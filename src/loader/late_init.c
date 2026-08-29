// Passively watches /proc/self/maps for GameAssembly.so and publishes the
// bridge after a short grace period. It never calls or patches IL2CPP during
// startup.
#define _GNU_SOURCE

#include "il2bridge/loader/late_init.h"
#include "il2bridge/loader/discovery.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    Il2BridgeReadyFn callback;
    void* user;
} ReadySubscriber;

static pthread_mutex_t g_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_watcher_once = PTHREAD_ONCE_INIT;
static ReadySubscriber g_ready_subscribers[IL2BRIDGE_READY_SUBSCRIBER_CAPACITY];
static int g_ready_subscriber_count;
static void* g_gameassembly_handle;
// Set when late_init_fire_ready_for_testing injected a non-dlopen handle so
// late_init_reset_for_testing knows it must not dlclose() it.
static bool g_gameassembly_handle_synthetic;
static struct timespec g_gameassembly_found_at;
static bool g_ready_fired;
static bool g_stop_requested;
static bool g_watcher_started;
static pthread_t g_watcher_thread;

enum {
    kWatcherTimeoutMilliseconds = 60000,
    kDefaultGraceMilliseconds = 5000,
};

typedef enum {
    FIND_NOT_FOUND,
    FIND_RETRY,
    FIND_FOUND,
} FindResult;

static long elapsed_milliseconds(const struct timespec* start, const struct timespec* now) {
    return (now->tv_sec - start->tv_sec) * 1000L + (now->tv_nsec - start->tv_nsec) / 1000000L;
}

static long configured_grace_milliseconds(void) {
    const char* value = getenv("IL2BRIDGE_WATCHER_GRACE_MS");
    if (!value || !*value) {
        return kDefaultGraceMilliseconds;
    }

    char* end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || *end != '\0' || parsed < 0 || parsed > kWatcherTimeoutMilliseconds) {
        fprintf(stderr, "[il2bridge] warning: invalid IL2BRIDGE_WATCHER_GRACE_MS='%s'; using %d ms\n",
                value, kDefaultGraceMilliseconds);
        return kDefaultGraceMilliseconds;
    }
    return parsed;
}

static void publish_ready(void) {
    ReadySubscriber snapshot[IL2BRIDGE_READY_SUBSCRIBER_CAPACITY];
    int count = 0;
    void* handle = NULL;

    pthread_mutex_lock(&g_state_mutex);
    if (g_ready_fired || !g_gameassembly_handle) {
        pthread_mutex_unlock(&g_state_mutex);
        return;
    }
    g_ready_fired = true;
    handle = g_gameassembly_handle;
    count = g_ready_subscriber_count;
    for (int i = 0; i < count; ++i) {
        snapshot[i] = g_ready_subscribers[i];
    }
    pthread_mutex_unlock(&g_state_mutex);

    if (count == 0) {
        fprintf(stderr, "[il2bridge] warning: GameAssembly.so became ready with no subscriber\n");
        return;
    }
    for (int i = 0; i < count; ++i) {
        snapshot[i].callback(handle, snapshot[i].user);
    }
}

// RTLD_NOLOAD obtains a dlsym-compatible reference without loading the
// module. Try its mapped path first and its ELF soname second.
static void* open_mapped_gameassembly(const char* path) {
    dlerror();
    void* handle = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) {
        dlerror();
        handle = dlopen("GameAssembly.so", RTLD_NOW | RTLD_NOLOAD);
    }
    return handle;
}

static FindResult find_gameassembly(bool log_handle_failure) {
    pthread_mutex_lock(&g_state_mutex);
    bool already_found = g_gameassembly_handle != NULL;
    pthread_mutex_unlock(&g_state_mutex);
    if (already_found) {
        return FIND_FOUND;
    }

    char path[4096];
    if (!discovery_find_module("GameAssembly.so", path, sizeof(path))) {
        return FIND_NOT_FOUND;
    }

    void* handle = open_mapped_gameassembly(path);
    if (!handle) {
        if (log_handle_failure) {
            const char* error = dlerror();
            fprintf(stderr, "[il2bridge] warning: GameAssembly.so is mapped but RTLD_NOLOAD failed: %s\n",
                    error ? error : "unknown error");
        }
        return FIND_RETRY;
    }

    pthread_mutex_lock(&g_state_mutex);
    if (!g_gameassembly_handle) {
        g_gameassembly_handle = handle;
        clock_gettime(CLOCK_MONOTONIC, &g_gameassembly_found_at);
        handle = NULL;
    }
    pthread_mutex_unlock(&g_state_mutex);
    if (handle) {
        dlclose(handle);
        return FIND_FOUND;
    }

    fprintf(stderr, "[il2bridge] found mapped GameAssembly.so at '%s'\n", path);
    return FIND_FOUND;
}

static long poll_interval_nanoseconds(long elapsed_ms) {
    if (elapsed_ms < 2000) {
        return 5000000L;
    }
    if (elapsed_ms < 10000) {
        return 25000000L;
    }
    return 100000000L;
}

static void* watcher_main(void* unused) {
    (void)unused;
    struct timespec started_at;
    clock_gettime(CLOCK_MONOTONIC, &started_at);
    long grace_ms = configured_grace_milliseconds();
    bool handle_failure_logged = false;

    for (;;) {
        pthread_mutex_lock(&g_state_mutex);
        bool stop_requested = g_stop_requested;
        pthread_mutex_unlock(&g_state_mutex);
        if (stop_requested) {
            return NULL;
        }

        FindResult result = find_gameassembly(!handle_failure_logged);
        if (result == FIND_RETRY) {
            handle_failure_logged = true;
        } else if (result == FIND_FOUND) {
            pthread_mutex_lock(&g_state_mutex);
            struct timespec found_at = g_gameassembly_found_at;
            pthread_mutex_unlock(&g_state_mutex);

            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (elapsed_milliseconds(&found_at, &now) >= grace_ms) {
                if (grace_ms > 0) {
                    fprintf(stderr, "[il2bridge] startup grace period complete after %ld ms\n", grace_ms);
                }
                publish_ready();
                return NULL;
            }
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = elapsed_milliseconds(&started_at, &now);
        // The timeout bounds discovery only. Once the module is found, always
        // honor the complete grace period even if it was mapped near 60 s.
        if (result != FIND_FOUND && elapsed_ms >= kWatcherTimeoutMilliseconds) {
            fprintf(stderr, "[il2bridge] warning: GameAssembly.so watcher timed out after %d seconds\n",
                    kWatcherTimeoutMilliseconds / 1000);
            return NULL;
        }

        struct timespec delay = {
            .tv_sec = 0,
            .tv_nsec = poll_interval_nanoseconds(elapsed_ms),
        };
        while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
        }
    }
}

static void start_watcher_once(void) {
    find_gameassembly(true);

    pthread_t thread;
    int rc = pthread_create(&thread, NULL, watcher_main, NULL);
    if (rc != 0) {
        fprintf(stderr, "[il2bridge] warning: failed to start GameAssembly.so watcher: %s\n", strerror(rc));
        return;
    }

    pthread_mutex_lock(&g_state_mutex);
    g_watcher_thread = thread;
    g_watcher_started = true;
    pthread_mutex_unlock(&g_state_mutex);
}

void late_init_start_gameassembly_watcher(void) {
    pthread_once(&g_watcher_once, start_watcher_once);
}

void late_init_stop_gameassembly_watcher(void) {
    pthread_mutex_lock(&g_state_mutex);
    g_stop_requested = true;
    bool started = g_watcher_started;
    pthread_t thread = g_watcher_thread;
    g_watcher_started = false;
    pthread_mutex_unlock(&g_state_mutex);

    if (started) {
        pthread_join(thread, NULL);
    }
}

bool late_init_add_ready_callback(Il2BridgeReadyFn on_ready, void* user) {
    if (!on_ready) {
        return false;
    }

    bool fire_now = false;
    void* handle = NULL;

    pthread_mutex_lock(&g_state_mutex);
    if (g_ready_subscriber_count >= IL2BRIDGE_READY_SUBSCRIBER_CAPACITY) {
        pthread_mutex_unlock(&g_state_mutex);
        return false;
    }
    g_ready_subscribers[g_ready_subscriber_count].callback = on_ready;
    g_ready_subscribers[g_ready_subscriber_count].user = user;
    ++g_ready_subscriber_count;
    if (g_ready_fired) {
        fire_now = true;
        handle = g_gameassembly_handle;
    }
    pthread_mutex_unlock(&g_state_mutex);

    // A consumer registering after the fact must not have to poll.
    if (fire_now) {
        on_ready(handle, user);
    }
    return true;
}

void late_init_fire_ready_for_testing(void* gameassembly_handle) {
    pthread_mutex_lock(&g_state_mutex);
    if (!g_gameassembly_handle) {
        g_gameassembly_handle = gameassembly_handle;
        // Injected handle is not from dlopen; reset must not dlclose() it.
        g_gameassembly_handle_synthetic = true;
    }
    pthread_mutex_unlock(&g_state_mutex);
    publish_ready();
}

bool late_init_check_now_for_testing(void) {
    return find_gameassembly(true) == FIND_FOUND;
}

void late_init_reset_for_testing(void) {
    pthread_mutex_lock(&g_state_mutex);
    void* handle = g_gameassembly_handle;
    bool synthetic = g_gameassembly_handle_synthetic;
    g_ready_subscriber_count = 0;
    g_gameassembly_handle = NULL;
    g_gameassembly_handle_synthetic = false;
    g_gameassembly_found_at = (struct timespec){0};
    g_ready_fired = false;
    pthread_mutex_unlock(&g_state_mutex);

    // Only real dlopen handles may be closed; a test-injected handle is not one.
    if (handle && !synthetic) {
        dlclose(handle);
    }
}
