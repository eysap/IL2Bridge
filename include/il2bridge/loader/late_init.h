#pragma once
#include "il2bridge/loader/api.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Registers the callback invoked once after GameAssembly.so has remained
// mapped for the configured startup grace period.
IL2BRIDGE_LOADER_API void late_init_set_ready_callback(void (*on_ready)(void* gameassembly_handle));

// Scans immediately for GameAssembly.so, then starts a bounded background
// watcher if it is not mapped yet. Safe to call more than once.
void late_init_start_gameassembly_watcher(void);

// Stops and joins the watcher so no loader code remains active at unload.
void late_init_stop_gameassembly_watcher(void);

// Test-only synchronous watcher iteration.
IL2BRIDGE_LOADER_API bool late_init_check_now_for_testing(void);

// Test-only state reset and release of the RTLD_NOLOAD reference.
IL2BRIDGE_LOADER_API void late_init_reset_for_testing(void);

#ifdef __cplusplus
}
#endif
