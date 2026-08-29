#include "il2bridge/loader/log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

#define IL2BRIDGE_LOG_LINE_MAX 512

static pthread_mutex_t g_sink_mutex = PTHREAD_MUTEX_INITIALIZER;
static Il2BridgeLogFn g_sink;
static void* g_sink_user;

void il2bridge_set_log_sink(Il2BridgeLogFn sink, void* user) {
    pthread_mutex_lock(&g_sink_mutex);
    g_sink = sink;
    g_sink_user = user;
    pthread_mutex_unlock(&g_sink_mutex);
}

void il2bridge_log(const char* format, ...) {
    char line[IL2BRIDGE_LOG_LINE_MAX];
    int written = snprintf(line, sizeof(line), "[il2bridge] ");
    if (written < 0 || (size_t)written >= sizeof(line)) {
        return;
    }

    va_list args;
    va_start(args, format);
    vsnprintf(line + written, sizeof(line) - (size_t)written, format, args);
    va_end(args);

    // Snapshot under the lock; never call a sink while holding it.
    pthread_mutex_lock(&g_sink_mutex);
    Il2BridgeLogFn sink = g_sink;
    void* user = g_sink_user;
    pthread_mutex_unlock(&g_sink_mutex);

    if (sink) {
        sink(line, user);
    } else {
        fprintf(stderr, "%s\n", line);
    }
}
