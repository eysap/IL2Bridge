#pragma once
#include "il2bridge/loader/api.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IL2BRIDGE_READY_SUBSCRIBER_CAPACITY 8

// Invoked once after GameAssembly.so has stayed mapped for the startup grace
// period. gameassembly_handle is the RTLD_NOLOAD reference; user is the
// pointer supplied at registration.
typedef void (*Il2BridgeReadyFn)(void* gameassembly_handle, void* user);

// Registers a readiness subscriber. If readiness has ALREADY fired, on_ready
// is invoked before this call returns, so a consumer loaded late never has to
// poll. Returns false for a NULL callback or once
// IL2BRIDGE_READY_SUBSCRIBER_CAPACITY subscribers are registered.
IL2BRIDGE_LOADER_API bool late_init_add_ready_callback(Il2BridgeReadyFn on_ready, void* user);

// Scans immediately for GameAssembly.so, then starts a bounded background
// watcher if it is not mapped yet. Safe to call more than once.
void late_init_start_gameassembly_watcher(void);

// Stops and joins the watcher so no loader code remains active at unload.
void late_init_stop_gameassembly_watcher(void);

// Test-only synchronous watcher iteration.
IL2BRIDGE_LOADER_API bool late_init_check_now_for_testing(void);

// Test-only readiness trigger; publishes to subscribers exactly once.
IL2BRIDGE_LOADER_API void late_init_fire_ready_for_testing(void* gameassembly_handle);

// Test-only state reset and release of the RTLD_NOLOAD reference.
IL2BRIDGE_LOADER_API void late_init_reset_for_testing(void);

#ifdef __cplusplus
}
#endif
