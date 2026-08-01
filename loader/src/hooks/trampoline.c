#include "hooks_internal.h"
#include "il2bridge/loader/hooks.h"
#include <Zydis/Zydis.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PATCH_SIZE 14 // jmp [rip+0] followed by an absolute address

// disp32/rel32 relocation requires the trampoline to remain within +/-2 GiB.
// MAP_FIXED_NOREPLACE probes free pages without replacing existing mappings.
static void* mmap_near(void* target, size_t len) {
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t target_page = (uintptr_t)target & ~(uintptr_t)(page_size - 1);

    // Preserve range for references originating anywhere in the mapping.
    const uintptr_t max_offset = (uintptr_t)INT32_MAX - (uintptr_t)page_size - len;

    for (uintptr_t offset = (uintptr_t)page_size; offset < max_offset; offset += (uintptr_t)page_size) {
        uintptr_t candidates[2] = { target_page + offset, target_page - offset };
        for (int i = 0; i < 2; ++i) {
            if (candidates[i] < (uintptr_t)page_size) {
                continue;
            }
            void* result = mmap((void*)candidates[i], len, PROT_READ | PROT_WRITE | PROT_EXEC,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (result != MAP_FAILED) {
                return result;
            }
            if (errno != EEXIST) {
                return MAP_FAILED;
            }
        }
    }

    return MAP_FAILED;
}

// Returns a whole-instruction patch length, or zero on decode failure.
static size_t compute_safe_patch_length(const void* target, size_t min_bytes) {
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    size_t consumed = 0;
    const unsigned char* cursor = (const unsigned char*)target;

    while (consumed < min_bytes) {
        ZydisDecodedInstruction instruction;
        ZyanStatus status = ZydisDecoderDecodeInstruction(&decoder, NULL, cursor, 16, &instruction);
        if (!ZYAN_SUCCESS(status)) {
            return 0;
        }
        consumed += instruction.length;
        cursor += instruction.length;
    }

    return consumed;
}

// Writes a signed relative field after checking its encoded width.
static bool patch_relative_field(unsigned char* field, uint8_t size_bits, int64_t new_value) {
    switch (size_bits) {
        case 8: {
            if (new_value < INT8_MIN || new_value > INT8_MAX) return false;
            int8_t v = (int8_t)new_value;
            memcpy(field, &v, sizeof(v));
            return true;
        }
        case 16: {
            if (new_value < INT16_MIN || new_value > INT16_MAX) return false;
            int16_t v = (int16_t)new_value;
            memcpy(field, &v, sizeof(v));
            return true;
        }
        case 32: {
            if (new_value < INT32_MIN || new_value > INT32_MAX) return false;
            int32_t v = (int32_t)new_value;
            memcpy(field, &v, sizeof(v));
            return true;
        }
        default:
            // Unsupported for x86-64 RIP-relative operands.
            return false;
    }
}

// Rewrites RIP-relative operands and relative control-flow immediates in a
// copied prologue. References move by target-tramp and must retain their
// original encoded width.
static bool relocate_patch_region(void* tramp, const void* target, size_t patch_len) {
    int64_t delta = (int64_t)((uintptr_t)target - (uintptr_t)tramp);

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    size_t consumed = 0;
    while (consumed < patch_len) {
        unsigned char* cursor = (unsigned char*)tramp + consumed;

        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        ZyanStatus status = ZydisDecoderDecodeFull(&decoder, cursor, patch_len - consumed, &instruction, operands);
        if (!ZYAN_SUCCESS(status)) {
            return false;
        }

        if (instruction.raw.disp.size != 0) {
            bool is_rip_relative = false;
            for (int i = 0; i < instruction.operand_count; ++i) {
                if (operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                    operands[i].mem.base == ZYDIS_REGISTER_RIP) {
                    is_rip_relative = true;
                    break;
                }
            }
            if (is_rip_relative) {
                int64_t new_disp = instruction.raw.disp.value + delta;
                if (!patch_relative_field(cursor + instruction.raw.disp.offset, instruction.raw.disp.size, new_disp)) {
                    return false;
                }
            }
        }

        // In-region branches would need retargeting to the copied instruction;
        // reject them until that relocation is implemented.
        for (int i = 0; i < 2; ++i) {
            if (instruction.raw.imm[i].size == 0 || !instruction.raw.imm[i].is_relative) {
                continue;
            }

            uintptr_t next_instruction_addr = (uintptr_t)target + consumed + instruction.length;
            uintptr_t destination = next_instruction_addr + (uintptr_t)instruction.raw.imm[i].value.s;
            if (destination >= (uintptr_t)target && destination < (uintptr_t)target + patch_len) {
                return false;
            }

            int64_t new_imm = instruction.raw.imm[i].value.s + delta;
            if (!patch_relative_field(cursor + instruction.raw.imm[i].offset, instruction.raw.imm[i].size, new_imm)) {
                return false;
            }
        }

        consumed += instruction.length;
    }

    return true;
}

// Uses an indirect RIP-relative jump so the trampoline tail preserves GPRs.
static void write_absolute_jump(unsigned char* at, void* destination) {
    at[0] = 0xFF; at[1] = 0x25; // jmp qword ptr [rip+0]
    at[2] = 0x00; at[3] = 0x00; at[4] = 0x00; at[5] = 0x00;
    memcpy(at + 6, &destination, sizeof(void*));
}

bool hook_install_trampoline(void* target, void* detour, HookHandle* out) {
    if (!target || !detour || !out) {
        return false;
    }

    // A duplicate would preserve the first hook's patch as original bytes.
    if (hook_registry_find_by_address(target)) {
        return false;
    }

    size_t patch_len = compute_safe_patch_length(target, PATCH_SIZE);
    if (patch_len == 0 || patch_len > sizeof(((HookEntry*)0)->original)) {
        return false;
    }

    void* tramp = mmap_near(target, patch_len + PATCH_SIZE);
    if (tramp == MAP_FAILED) {
        return false;
    }

    memcpy(tramp, target, patch_len);

    if (!relocate_patch_region(tramp, target, patch_len)) {
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }

    write_absolute_jump((unsigned char*)tramp + patch_len, (unsigned char*)target + patch_len);

    if (!unprotect_code(target, patch_len)) {
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }

    HookEntry entry = {0};
    entry.target = target;
    entry.detour = detour;
    entry.trampoline = tramp;
    entry.type = HOOK_TYPE_TRAMPOLINE;
    entry.mode = HOOK_MODE_REPLACE;
    memcpy(entry.original, target, patch_len);
    entry.original_len = (unsigned char)patch_len;
    entry.disabled = false;

    // Publish before patching so every live detour has an uninstall handle.
    if (!hook_registry_add(&entry, out)) {
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }

    unsigned char patch[PATCH_SIZE];
    write_absolute_jump(patch, detour);
    memcpy(target, patch, PATCH_SIZE);
    if (patch_len > PATCH_SIZE) {
        memset((unsigned char*)target + PATCH_SIZE, 0x90, patch_len - PATCH_SIZE); // NOP-pad
    }
    __builtin___clear_cache((char*)target, (char*)target + patch_len);

    return true;
}

bool hook_install_counter_probe(void* target, HookHandle* out) {
#if !defined(__x86_64__)
    (void)target;
    (void)out;
    return false;
#else
    if (!target || !out || hook_registry_find_by_address(target)) {
        return false;
    }

    size_t patch_len = compute_safe_patch_length(target, PATCH_SIZE);
    if (patch_len == 0 || patch_len > sizeof(((HookEntry*)0)->original)) {
        return false;
    }

    void* tramp = mmap_near(target, patch_len + PATCH_SIZE);
    if (tramp == MAP_FAILED) {
        return false;
    }
    memcpy(tramp, target, patch_len);
    if (!relocate_patch_region(tramp, target, patch_len)) {
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }
    write_absolute_jump((unsigned char*)tramp + patch_len, (unsigned char*)target + patch_len);

    CounterProbeState* state = calloc(1, sizeof(*state));
    if (!state) {
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        free(state);
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }
    unsigned char* stub = mmap(NULL, (size_t)page_size,
                               PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stub == MAP_FAILED) {
        free(state);
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }

    // movabs r11,&count; lock incq [r11]; movabs r11,trampoline; jmp r11
    // r11 and flags are caller-clobbered in the SysV x86-64 ABI, while all
    // integer/floating arguments remain untouched for the original body.
    size_t cursor = 0;
    stub[cursor++] = 0x49; stub[cursor++] = 0xBB;
    void* count_address = &state->hit_count;
    memcpy(stub + cursor, &count_address, sizeof(count_address)); cursor += sizeof(count_address);
    stub[cursor++] = 0xF0; stub[cursor++] = 0x49; stub[cursor++] = 0xFF; stub[cursor++] = 0x03;
    stub[cursor++] = 0x49; stub[cursor++] = 0xBB;
    memcpy(stub + cursor, &tramp, sizeof(tramp)); cursor += sizeof(tramp);
    stub[cursor++] = 0x41; stub[cursor++] = 0xFF; stub[cursor++] = 0xE3;
    __builtin___clear_cache((char*)stub, (char*)stub + cursor);

    if (!unprotect_code(target, patch_len)) {
        munmap(stub, (size_t)page_size);
        free(state);
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }

    HookEntry entry = {0};
    entry.target = target;
    entry.detour = stub;
    entry.trampoline = tramp;
    entry.type = HOOK_TYPE_TRAMPOLINE;
    entry.mode = HOOK_MODE_AROUND;
    entry.probe_state = state;
    entry.owned_detour = stub;
    entry.owned_detour_len = (size_t)page_size;
    memcpy(entry.original, target, patch_len);
    entry.original_len = (unsigned char)patch_len;

    if (!hook_registry_add(&entry, out)) {
        munmap(stub, (size_t)page_size);
        free(state);
        munmap(tramp, patch_len + PATCH_SIZE);
        return false;
    }

    unsigned char patch[PATCH_SIZE];
    write_absolute_jump(patch, stub);
    memcpy(target, patch, PATCH_SIZE);
    if (patch_len > PATCH_SIZE) {
        memset((unsigned char*)target + PATCH_SIZE, 0x90, patch_len - PATCH_SIZE);
    }
    __builtin___clear_cache((char*)target, (char*)target + patch_len);
    return true;
#endif
}

bool hook_probe_hit_count(HookHandle handle, uint64_t* out_count) {
    if (!out_count) return false;
    const HookEntry* entry = hook_registry_get(handle);
    if (!entry || !entry->probe_state) {
        return false;
    }
    CounterProbeState* state = (CounterProbeState*)entry->probe_state;
    *out_count = __atomic_load_n(&state->hit_count, __ATOMIC_RELAXED);
    return true;
}

bool hook_uninstall_trampoline(HookHandle handle) {
    const HookEntry* entry = hook_registry_get(handle);
    if (!entry || entry->type != HOOK_TYPE_TRAMPOLINE) {
        return false;
    }

    memcpy(entry->target, entry->original, entry->original_len);
    __builtin___clear_cache((char*)entry->target, (char*)entry->target + entry->original_len);

    if (!hook_registry_disable(handle)) {
        return false;
    }

    if (entry->trampoline) {
        munmap(entry->trampoline, (size_t)entry->original_len + PATCH_SIZE);
    }

    if (entry->owned_detour) {
        munmap(entry->owned_detour, entry->owned_detour_len);
    }
    free(entry->probe_state);
    return true;
}

void* hook_manager_get_trampoline(HookHandle handle) {
    const HookEntry* entry = hook_registry_get(handle);
    if (!entry || entry->type != HOOK_TYPE_TRAMPOLINE) {
        return NULL;
    }
    return entry->trampoline;
}
