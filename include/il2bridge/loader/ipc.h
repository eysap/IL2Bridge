#pragma once
#include "il2bridge/loader/api.h"
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Splits a mutable request line in place. Results point into `line`; arguments
// beyond max_args are ignored. Returns -1 for an empty line and leaves outputs
// untouched. The caller removes line-ending bytes before parsing.
IL2BRIDGE_LOADER_API int ipc_parse_line(char* line, const char** command_out, const char** args_out, int max_args);

// Builds the per-pid socket path. Prefers
// $XDG_RUNTIME_DIR/il2bridge/<pid>.sock and falls back to /tmp when no runtime
// directory is configured. The server creates a private runtime directory.
IL2BRIDGE_LOADER_API void ipc_socket_path(pid_t pid, char* out, size_t out_size);

typedef struct IpcServer IpcServer;

// Starts the per-process server and its joinable accept-loop thread.
// Returns NULL on setup failure or when a live server owns the path.
IL2BRIDGE_LOADER_API IpcServer* ipc_server_start(void);

// Stops and joins the server, removes its socket, and frees it. NULL is valid.
IL2BRIDGE_LOADER_API void ipc_server_stop(IpcServer* server);

#ifdef __cplusplus
}
#endif
