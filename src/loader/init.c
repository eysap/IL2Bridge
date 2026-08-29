// LD_PRELOAD process glue. The constructor starts the GameAssembly watcher;
// the IPC server is exposed only after late_init's readiness gate and after
// bridge_init has resolved its required API.

#include "il2bridge/loader/late_init.h"
#include "il2bridge/loader/bridge.h"
#include "il2bridge/loader/ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// The callback and destructor can run on different threads.
static IpcServer* g_server = NULL;

static void on_gameassembly_ready(void* gameassembly_handle, void* user) {
    (void)user;
    if (!bridge_init(gameassembly_handle)) {
        fprintf(stderr, "[il2bridge] failed to resolve required il2cpp_* symbols\n");
        return;
    }

    IpcServer* server = ipc_server_start();
    if (!server) {
        fprintf(stderr, "[il2bridge] failed to start IPC server\n");
        return;
    }
    __atomic_store_n(&g_server, server, __ATOMIC_SEQ_CST);

    fprintf(stderr, "[il2bridge] loader ready (pid %d)\n", (int)getpid());
}

__attribute__((constructor))
static void il2bridge_loader_init(void) {
    if (!late_init_add_ready_callback(on_gameassembly_ready, NULL)) {
        fprintf(stderr, "[il2bridge] failed to register the readiness callback\n");
        return;
    }
    fprintf(stderr, "[il2bridge] loader injected, watching for GameAssembly.so\n");
    if (!getenv("IL2BRIDGE_DISABLE_WATCHER")) {
        late_init_start_gameassembly_watcher();
    }
}

__attribute__((destructor))
static void il2bridge_loader_fini(void) {
    late_init_stop_gameassembly_watcher();
    IpcServer* server = __atomic_exchange_n(&g_server, NULL, __ATOMIC_SEQ_CST);
    if (server) {
        ipc_server_stop(server);
    }
}
