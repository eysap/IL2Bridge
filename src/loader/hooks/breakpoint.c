// glibc exposes REG_RIP under _GNU_SOURCE.
#define _GNU_SOURCE

#include "hooks_internal.h"
#include "il2bridge/loader/hooks.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

static bool g_sigtrap_installed = false;
static struct sigaction g_prev_sigtrap;

static void sigtrap_handler(int sig, siginfo_t* info, void* uctx_raw) {
    (void)info;
    ucontext_t* uctx = (ucontext_t*)uctx_raw;
#if defined(__x86_64__)
    uintptr_t rip = (uintptr_t)uctx->uc_mcontext.gregs[REG_RIP];
    const HookEntry* entry = hook_registry_find_by_address((void*)(rip - 1));
    if (entry && entry->type == HOOK_TYPE_BREAKPOINT) {
        uctx->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)entry->detour;
        return;
    }
#endif
    if (g_prev_sigtrap.sa_flags & SA_SIGINFO) {
        if (g_prev_sigtrap.sa_sigaction) {
            g_prev_sigtrap.sa_sigaction(sig, info, uctx_raw);
        }
    } else if (g_prev_sigtrap.sa_handler && g_prev_sigtrap.sa_handler != SIG_DFL && g_prev_sigtrap.sa_handler != SIG_IGN) {
        g_prev_sigtrap.sa_handler(sig);
    }
}

static bool install_sigtrap_handler_once(void) {
    if (g_sigtrap_installed) {
        return true;
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigtrap_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGTRAP, &sa, &g_prev_sigtrap) != 0) {
        // Keep the flag clear so a later installation can retry.
        return false;
    }
    g_sigtrap_installed = true;
    return true;
}

static bool install_breakpoint(void* target, void* detour,
                               CounterProbeState* state,
                               void* owned_detour, size_t owned_detour_len,
                               HookHandle* out) {
    if (!target || !detour || !out) {
        return false;
    }

    // A duplicate would preserve 0xCC as the original byte.
    if (hook_registry_find_by_address(target)) {
        return false;
    }

    if (!unprotect_code(target, 1)) {
        return false;
    }

    if (!install_sigtrap_handler_once()) {
        return false;
    }

    HookEntry entry = {0};
    entry.target = target;
    entry.detour = detour;
    entry.trampoline = NULL;
    entry.type = HOOK_TYPE_BREAKPOINT;
    entry.mode = HOOK_MODE_REPLACE;
    entry.probe_state = state;
    entry.owned_detour = owned_detour;
    entry.owned_detour_len = owned_detour_len;
    entry.original[0] = *(unsigned char*)target;
    entry.original_len = 1;
    entry.disabled = false;

    // Publish first so SIGTRAP can resolve the entry as soon as INT3 is live.
    if (!hook_registry_add(&entry, out)) {
        return false;
    }

    *(unsigned char*)target = 0xCC;
    __builtin___clear_cache((char*)target, (char*)target + 1);

    return true;
}

bool hook_install_breakpoint(void* target, void* detour, HookHandle* out) {
    return install_breakpoint(target, detour, NULL, NULL, 0, out);
}

bool hook_install_replacement_counter(void* target, HookHandle* out) {
#if !defined(__x86_64__)
    (void)target;
    (void)out;
    return false;
#else
    if (!target || !out) return false;
    CounterProbeState* state = calloc(1, sizeof(*state));
    if (!state) return false;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        free(state);
        return false;
    }
    unsigned char* stub = mmap(NULL, (size_t)page_size,
                               PROT_READ | PROT_WRITE | PROT_EXEC,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stub == MAP_FAILED) {
        free(state);
        return false;
    }
    // movabs r11,&count; lock incq [r11]; ret
    size_t cursor = 0;
    stub[cursor++] = 0x49; stub[cursor++] = 0xBB;
    void* count_address = &state->hit_count;
    memcpy(stub + cursor, &count_address, sizeof(count_address)); cursor += sizeof(count_address);
    stub[cursor++] = 0xF0; stub[cursor++] = 0x49; stub[cursor++] = 0xFF; stub[cursor++] = 0x03;
    stub[cursor++] = 0xC3;
    __builtin___clear_cache((char*)stub, (char*)stub + cursor);

    if (!install_breakpoint(target, stub, state, stub, (size_t)page_size, out)) {
        munmap(stub, (size_t)page_size);
        free(state);
        return false;
    }
    return true;
#endif
}

bool hook_uninstall_breakpoint(HookHandle handle) {
    const HookEntry* entry = hook_registry_get(handle);
    if (!entry || entry->type != HOOK_TYPE_BREAKPOINT) {
        return false;
    }

    *(unsigned char*)entry->target = entry->original[0];
    __builtin___clear_cache((char*)entry->target, (char*)entry->target + 1);

    if (!hook_registry_disable(handle)) {
        return false;
    }
    if (entry->owned_detour) {
        munmap(entry->owned_detour, entry->owned_detour_len);
    }
    free(entry->probe_state);
    return true;
}
