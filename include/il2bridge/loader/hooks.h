#pragma once
#include "il2bridge/loader/api.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOOK_REGISTRY_CAPACITY 256

typedef enum {
    HOOK_TYPE_BREAKPOINT,
    HOOK_TYPE_TRAMPOLINE,
} HookType;

typedef enum {
    HOOK_MODE_REPLACE,
    HOOK_MODE_AROUND,
} HookMode;

typedef struct {
    uint32_t slot;
} HookHandle;

typedef struct {
    void* target;
    void* detour;
    void* trampoline;      // only meaningful for HOOK_TYPE_TRAMPOLINE
    HookType type;
    HookMode mode;
    void* probe_state;     // owned by an around hook, otherwise NULL
    void* owned_detour;    // generated around stub, otherwise NULL
    size_t owned_detour_len;
    unsigned char original[32]; // original bytes; 28 is the x86-64 worst case
    unsigned char original_len;
    // Kept ABI-compatible with C++ tests. Published entries must access this
    // field through the __atomic builtins in registry.c.
    bool disabled;
} HookEntry;

// Publishes a fully initialized entry and returns its handle. Calls that
// mutate the registry must be serialized; readers are lock-free.
IL2BRIDGE_LOADER_API bool hook_registry_add(const HookEntry* entry, HookHandle* out);

// Disables an entry permanently. Registry slots are not reused.
IL2BRIDGE_LOADER_API bool hook_registry_disable(HookHandle handle);

// Returns an enabled entry. This lookup is signal-safe.
IL2BRIDGE_LOADER_API const HookEntry* hook_registry_get(HookHandle handle);

// Finds an enabled entry by target address. This lookup is signal-safe.
IL2BRIDGE_LOADER_API const HookEntry* hook_registry_find_by_address(void* address);

// Test-only registry reset.
IL2BRIDGE_LOADER_API void hook_registry_reset_for_testing(void);

// Installs an x86-64 INT3 replacement hook. The process-wide SIGTRAP handler
// redirects RIP to detour; the original function is not called. Duplicate
// targets are rejected. Install and uninstall operations must be serialized.
IL2BRIDGE_LOADER_API bool hook_install_breakpoint(void* target, void* detour, HookHandle* out);

// Installs a counting replacement probe for a void method.
IL2BRIDGE_LOADER_API bool hook_install_replacement_counter(void* target, HookHandle* out);

// Restores a breakpoint hook and disables its registry entry.
IL2BRIDGE_LOADER_API bool hook_uninstall_breakpoint(HookHandle handle);

// Installs an x86-64 trampoline hook. Whole instructions are relocated into
// nearby executable memory and target is patched with a 14-byte absolute
// jump. Duplicate targets are rejected; mutations must be serialized.
//
// Current limits: the saved prologue is 32 bytes, in-region relative branches
// and rel8 relocation are rejected, function boundaries are not known, and
// the target patch is not atomic. Callers must treat installation/removal in a
// running multithreaded process as a coordinated operation.
IL2BRIDGE_LOADER_API bool hook_install_trampoline(void* target, void* detour, HookHandle* out);

// Installs a counting around probe that tail-jumps to the trampoline.
IL2BRIDGE_LOADER_API bool hook_install_counter_probe(void* target, HookHandle* out);

// Reads the current hit count of an enabled around counter probe.
IL2BRIDGE_LOADER_API bool hook_probe_hit_count(HookHandle handle, uint64_t* out_count);

// Restores a trampoline hook, releases owned mappings, and disables its entry.
IL2BRIDGE_LOADER_API bool hook_uninstall_trampoline(HookHandle handle);

// Returns the callable original-body trampoline for an enabled hook.
IL2BRIDGE_LOADER_API void* hook_manager_get_trampoline(HookHandle handle);

#ifdef __cplusplus
}
#endif
