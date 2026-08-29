#include "il2bridge/loader/hooks.h"
#include "il2bridge/loader/log.h"

static HookEntry g_entries[HOOK_REGISTRY_CAPACITY];

// Accessed only through __atomic builtins for C/C++ header compatibility.
static uint32_t g_published_count;

bool hook_registry_add(const HookEntry* entry, HookHandle* out) {
    if (!entry || !out) {
        return false;
    }

    uint32_t slot = __atomic_load_n(&g_published_count, __ATOMIC_RELAXED);
    if (slot >= HOOK_REGISTRY_CAPACITY) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            il2bridge_log("error: hook registry exhausted at %d slots; "
                          "slots are never reused, install hooks once and gate dispatch",
                          HOOK_REGISTRY_CAPACITY);
        }
        return false;
    }

    g_entries[slot] = *entry;
    g_entries[slot].disabled = false;

    // Publish: everything above must be visible to any reader that observes
    // the incremented count (release pairs with the acquire load in
    // hook_registry_get/find_by_address).
    __atomic_store_n(&g_published_count, slot + 1, __ATOMIC_RELEASE);

    out->slot = slot;
    return true;
}

bool hook_registry_disable(HookHandle handle) {
    uint32_t count = __atomic_load_n(&g_published_count, __ATOMIC_ACQUIRE);
    if (handle.slot >= count) {
        return false;
    }

    bool expected = false;
    return __atomic_compare_exchange_n(
        &g_entries[handle.slot].disabled, &expected, true,
        /*weak=*/false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

const HookEntry* hook_registry_get(HookHandle handle) {
    uint32_t count = __atomic_load_n(&g_published_count, __ATOMIC_ACQUIRE);
    if (handle.slot >= count) {
        return NULL;
    }
    if (__atomic_load_n(&g_entries[handle.slot].disabled, __ATOMIC_ACQUIRE)) {
        return NULL;
    }
    return &g_entries[handle.slot];
}

const HookEntry* hook_registry_find_by_address(void* address) {
    uint32_t count = __atomic_load_n(&g_published_count, __ATOMIC_ACQUIRE);
    for (uint32_t i = 0; i < count; ++i) {
        if (__atomic_load_n(&g_entries[i].disabled, __ATOMIC_ACQUIRE)) {
            continue;
        }
        if (g_entries[i].target == address) {
            return &g_entries[i];
        }
    }
    return NULL;
}

void hook_registry_reset_for_testing(void) {
    __atomic_store_n(&g_published_count, 0, __ATOMIC_RELAXED);
}

uint32_t hook_registry_used(void) {
    return __atomic_load_n(&g_published_count, __ATOMIC_ACQUIRE);
}
