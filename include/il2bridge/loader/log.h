#pragma once
#include "il2bridge/loader/api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Receives one fully formatted line, already prefixed with "[il2bridge] " and
// without a trailing newline. Called on whichever thread emitted the message.
typedef void (*Il2BridgeLogFn)(const char* line, void* user);

// Replaces the diagnostic sink. Passing NULL restores the default, which
// writes to stderr. An embedder whose host application takes over file
// descriptor 2 should install its own sink. Not signal-safe; do not call
// from a signal handler. The installed sink must not call back into
// il2bridge_log() (reentrancy is not supported).
IL2BRIDGE_LOADER_API void il2bridge_set_log_sink(Il2BridgeLogFn sink, void* user);

// Emits one diagnostic line. Lines longer than the internal buffer are
// truncated rather than allocating or overflowing. Takes a mutex and is not
// signal-safe; do not call from a signal handler.
IL2BRIDGE_LOADER_API void il2bridge_log(const char* format, ...)
    __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
