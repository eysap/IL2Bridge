#include "il2bridge/loader/ipc.h"
#include "il2bridge/loader/bridge.h"
#include "il2bridge/loader/hooks.h"
#include "il2bridge/loader/handlers.h"
#include "il2bridge/loader/events.h"
#include "il2bridge/loader/log.h"
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define IPC_LISTEN_BACKLOG 16

// Maximum request size including the line terminator.
#define IPC_LINE_BUFFER_SIZE 512

#define IPC_MAX_ARGS 8

// Prevent an idle client from blocking the single-threaded server or stop().
#define IPC_CONNECTION_RECV_TIMEOUT_SEC 2

struct IpcServer {
    int listen_fd;
    // Wakes poll() during shutdown without racing a cross-thread close().
    int stop_pipe[2];
    pthread_t thread;
    char socket_path[sizeof(((struct sockaddr_un*)0)->sun_path)];
};

// Serializes bind/unlink transitions for the per-process socket path.
static pthread_mutex_t g_server_lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;

void ipc_socket_path(pid_t pid, char* out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && runtime[0] != '\0') {
        int n = snprintf(out, out_size, "%s/il2bridge/%d.sock", runtime, (int)pid);
        if (n > 0 && (size_t)n < out_size && (size_t)n < sizeof(((struct sockaddr_un*)0)->sun_path)) return;
    }
    snprintf(out, out_size, "/tmp/il2bridge-%d.sock", (int)pid);
}

static bool prepare_runtime_socket_path(pid_t pid, char* out, size_t out_size) {
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && runtime[0] != '\0') {
        char directory[sizeof(((struct sockaddr_un*)0)->sun_path)];
        int n = snprintf(directory, sizeof(directory), "%s/il2bridge", runtime);
        if (n > 0 && (size_t)n < sizeof(directory)) {
            if (mkdir(directory, S_IRWXU) == 0 || errno == EEXIST) {
                struct stat status;
                if (lstat(directory, &status) == 0 && S_ISDIR(status.st_mode) &&
                    status.st_uid == geteuid() && (status.st_mode & 0077) == 0) {
                    n = snprintf(out, out_size, "%s/%d.sock", directory, (int)pid);
                    if (n > 0 && (size_t)n < out_size &&
                        (size_t)n < sizeof(((struct sockaddr_un*)0)->sun_path)) return true;
                }
            }
        }
    }
    int n = snprintf(out, out_size, "/tmp/il2bridge-%d.sock", (int)pid);
    return n > 0 && (size_t)n < out_size;
}

static ssize_t write_full(int fd, const char* data, size_t len) {
    size_t total = 0;
    while (total < len) {
        // A disconnected client must not deliver SIGPIPE to the host process.
        ssize_t n = send(fd, data + total, len - total, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            break; // peer gone; nothing more we can do
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

// IPC exposes opaque indices rather than accepting process addresses. These
// tables and all hook mutations are owned by the accept-loop thread.
#define MAX_IMAGE_HANDLES 32
#define MAX_CLASS_HANDLES 256
#define MAX_METHOD_HANDLES 1024

#define IPC_NAME_SIZE 192

static Il2CppImage* g_images[MAX_IMAGE_HANDLES];
static char g_image_names[MAX_IMAGE_HANDLES][IPC_NAME_SIZE];
static int g_image_count;
static Il2CppClass* g_classes[MAX_CLASS_HANDLES];
static int g_class_image_handles[MAX_CLASS_HANDLES];
static char g_class_namespaces[MAX_CLASS_HANDLES][IPC_NAME_SIZE];
static char g_class_names[MAX_CLASS_HANDLES][IPC_NAME_SIZE];
static int g_class_count;
static Il2CppMethod* g_methods[MAX_METHOD_HANDLES];
static int g_method_class_handles[MAX_METHOD_HANDLES];
static char g_method_names[MAX_METHOD_HANDLES][IPC_NAME_SIZE];
static int g_method_arities[MAX_METHOD_HANDLES];
static unsigned int g_method_tokens[MAX_METHOD_HANDLES];
static int g_method_count;

typedef struct {
    bool known;
    int method_handle;
    char mode[16];
    char transport[16];
    char handler[48];
} HookMetadata;

static HookMetadata g_hook_metadata[HOOK_REGISTRY_CAPACITY];
static uint64_t g_last_streamed_hits[HOOK_REGISTRY_CAPACITY];

static void copy_ipc_name(char* destination, size_t destination_size, const char* source) {
    if (!destination || destination_size == 0) return;
    snprintf(destination, destination_size, "%s", source ? source : "");
}

// Reuse handles for stable IL2CPP pointers to avoid exhausting fixed tables.
static int store_image(Il2CppImage* image, const char* assembly_name) {
    for (int i = 0; i < g_image_count; ++i) {
        if (g_images[i] == image) {
            return i;
        }
    }
    if (g_image_count >= MAX_IMAGE_HANDLES) {
        return -1;
    }
    g_images[g_image_count] = image;
    copy_ipc_name(g_image_names[g_image_count], sizeof(g_image_names[g_image_count]), assembly_name);
    return g_image_count++;
}

static int store_class(Il2CppClass* klass, int image_handle, const char* namespaze, const char* class_name) {
    for (int i = 0; i < g_class_count; ++i) {
        if (g_classes[i] == klass) {
            return i;
        }
    }
    if (g_class_count >= MAX_CLASS_HANDLES) {
        return -1;
    }
    g_classes[g_class_count] = klass;
    g_class_image_handles[g_class_count] = image_handle;
    copy_ipc_name(g_class_namespaces[g_class_count], sizeof(g_class_namespaces[g_class_count]), namespaze);
    copy_ipc_name(g_class_names[g_class_count], sizeof(g_class_names[g_class_count]), class_name);
    return g_class_count++;
}

static int store_method(Il2CppMethod* method, int class_handle, const char* method_name, int arity) {
    for (int i = 0; i < g_method_count; ++i) {
        if (g_methods[i] == method) {
            return i;
        }
    }
    if (g_method_count >= MAX_METHOD_HANDLES) {
        return -1;
    }
    g_methods[g_method_count] = method;
    g_method_class_handles[g_method_count] = class_handle;
    copy_ipc_name(g_method_names[g_method_count], sizeof(g_method_names[g_method_count]), method_name);
    g_method_arities[g_method_count] = arity;
    g_method_tokens[g_method_count] = bridge_method_token(method);
    return g_method_count++;
}

// Parses a non-negative decimal integer without accepting partial input.
static bool parse_nonneg_int(const char* str, long* out) {
    if (!str || *str == '\0') {
        return false;
    }
    char* end = NULL;
    errno = 0;
    long value = strtol(str, &end, 10);
    if (errno != 0 || end == str || *end != '\0' || value < 0) {
        return false;
    }
    *out = value;
    return true;
}

static void write_ok_int(int client_fd, int value) {
    char resp[32];
    int n = snprintf(resp, sizeof(resp), "OK %d\n", value);
    if (n < 0) {
        return;
    }
    write_full(client_fd, resp, (size_t)n);
}

static void write_ok_uint(int client_fd, unsigned int value) {
    char resp[32];
    int n = snprintf(resp, sizeof(resp), "OK %u\n", value);
    if (n < 0) {
        return;
    }
    write_full(client_fd, resp, (size_t)n);
}

static void handle_resolve_image(int client_fd, int argc, const char** args) {
    if (argc < 1) {
        write_full(client_fd, "ERR bad-args\n", 13);
        return;
    }

    Il2CppImage* image = bridge_find_image(args[0]);
    if (!image) {
        write_full(client_fd, "ERR not-found\n", 14);
        return;
    }

    int handle = store_image(image, args[0]);
    if (handle < 0) {
        write_full(client_fd, "ERR table-full\n", 15);
        return;
    }

    write_ok_int(client_fd, handle);
}

static void handle_resolve_class(int client_fd, int argc, const char** args) {
    if (argc < 3) {
        write_full(client_fd, "ERR bad-args\n", 13);
        return;
    }

    long image_handle;
    if (!parse_nonneg_int(args[0], &image_handle) || image_handle >= g_image_count) {
        write_full(client_fd, "ERR bad-handle\n", 15);
        return;
    }

    const char* namespaze = strcmp(args[1], "-") == 0 ? "" : args[1];
    Il2CppClass* klass = bridge_find_class(g_images[image_handle], namespaze, args[2]);
    if (!klass) {
        write_full(client_fd, "ERR not-found\n", 14);
        return;
    }

    int handle = store_class(klass, (int)image_handle, namespaze, args[2]);
    if (handle < 0) {
        write_full(client_fd, "ERR table-full\n", 15);
        return;
    }

    write_ok_int(client_fd, handle);
}

static void handle_resolve_method(int client_fd, int argc, const char** args) {
    if (argc < 3) {
        write_full(client_fd, "ERR bad-args\n", 13);
        return;
    }

    long class_handle;
    if (!parse_nonneg_int(args[0], &class_handle) || class_handle >= g_class_count) {
        write_full(client_fd, "ERR bad-handle\n", 15);
        return;
    }

    // Bound before the cast to int and reject implausible method arities.
    long arg_count;
    if (!parse_nonneg_int(args[2], &arg_count) || arg_count > 255) {
        write_full(client_fd, "ERR bad-arg-count\n", 18);
        return;
    }

    bool ambiguous = false;
    Il2CppMethod* method = bridge_find_method_unique(g_classes[class_handle], args[1],
                                                     (int)arg_count, &ambiguous);
    if (!method) {
        if (ambiguous) write_full(client_fd, "ERR ambiguous\n", 14);
        else write_full(client_fd, "ERR not-found\n", 14);
        return;
    }

    int handle = store_method(method, (int)class_handle, args[1], (int)arg_count);
    if (handle < 0) {
        write_full(client_fd, "ERR table-full\n", 15);
        return;
    }

    write_ok_int(client_fd, handle);
}

static void handle_resolve_method_token(int client_fd, int argc, const char** args) {
    if (argc < 2) {
        write_full(client_fd, "ERR bad-args\n", 13);
        return;
    }
    long class_handle;
    if (!parse_nonneg_int(args[0], &class_handle) || class_handle >= g_class_count) {
        write_full(client_fd, "ERR bad-handle\n", 15);
        return;
    }
    errno = 0;
    char* end = NULL;
    unsigned long token = strtoul(args[1], &end, 0);
    if (errno != 0 || end == args[1] || *end != '\0' || token == 0 || token > UINT32_MAX) {
        write_full(client_fd, "ERR bad-token\n", 14);
        return;
    }
    Il2CppMethod* method = bridge_find_method_by_token(g_classes[class_handle], (unsigned int)token);
    if (!method) {
        write_full(client_fd, "ERR not-found\n", 14);
        return;
    }
    int handle = store_method(method, (int)class_handle, argc >= 3 ? args[2] : "-", -1);
    if (handle < 0) {
        write_full(client_fd, "ERR table-full\n", 15);
        return;
    }
    write_ok_int(client_fd, handle);
}

static void handle_hook(int client_fd, int argc, const char** args) {
    if (argc < 4) {
        write_full(client_fd, "ERR bad-args\n", 13);
        return;
    }

    long method_handle;
    if (!parse_nonneg_int(args[0], &method_handle) || method_handle >= g_method_count) {
        write_full(client_fd, "ERR bad-handle\n", 15);
        return;
    }

    bool is_around;
    if (strcmp(args[1], "replace") == 0) {
        is_around = false;
    } else if (strcmp(args[1], "around") == 0) {
        is_around = true;
    } else {
        write_full(client_fd, "ERR bad-hook-mode\n", 18);
        return;
    }

    bool is_breakpoint;
    if (strcmp(args[2], "breakpoint") == 0) {
        is_breakpoint = true;
    } else if (strcmp(args[2], "trampoline") == 0) {
        is_breakpoint = false;
    } else {
        write_full(client_fd, "ERR bad-hook-type\n", 18);
        return;
    }

    void* target = bridge_method_pointer(g_methods[method_handle]);
    if (!target) {
        write_full(client_fd, "ERR no-method-pointer\n", 22);
        return;
    }

    HookHandle hook_handle;
    bool installed;
    if (is_around) {
        if (is_breakpoint) {
            write_full(client_fd, "ERR around-needs-trampoline\n", 28);
            return;
        }
        if (strcmp(args[3], "count-calls") != 0) {
            write_full(client_fd, "ERR unknown-handler\n", 20);
            return;
        }
        installed = hook_install_counter_probe(target, &hook_handle);
    } else {
        if (strcmp(args[3], "count-calls") == 0) {
            if (!is_breakpoint) {
                write_full(client_fd, "ERR replace-counter-needs-breakpoint\n", 37);
                return;
            }
            installed = hook_install_replacement_counter(target, &hook_handle);
        } else {
            const HandlerEntry* handler = handler_lookup(args[3]);
            if (!handler) {
                write_full(client_fd, "ERR unknown-handler\n", 20);
                return;
            }
            installed = is_breakpoint
                ? hook_install_breakpoint(target, handler->function_pointer, &hook_handle)
                : hook_install_trampoline(target, handler->function_pointer, &hook_handle, NULL);
        }
    }

    if (!installed) {
        write_full(client_fd, "ERR hook-install-failed\n", 24);
        return;
    }

    if (hook_handle.slot < HOOK_REGISTRY_CAPACITY) {
        HookMetadata* metadata = &g_hook_metadata[hook_handle.slot];
        metadata->known = true;
        metadata->method_handle = (int)method_handle;
        copy_ipc_name(metadata->mode, sizeof(metadata->mode), args[1]);
        copy_ipc_name(metadata->transport, sizeof(metadata->transport), args[2]);
        copy_ipc_name(metadata->handler, sizeof(metadata->handler), args[3]);
        g_last_streamed_hits[hook_handle.slot] = 0;
        char payload[IL2BRIDGE_EVENT_PAYLOAD_SIZE];
        snprintf(payload, sizeof(payload), "mode=%s,transport=%s,handler=%s", args[1], args[2], args[3]);
        event_stream_publish(hook_handle.slot, "hook-added", payload);
    }

    write_ok_uint(client_fd, hook_handle.slot);
}

static unsigned long long process_start_ticks(void) {
    FILE* stat_file = fopen("/proc/self/stat", "r");
    if (!stat_file) return 0;
    char buffer[4096];
    if (!fgets(buffer, sizeof(buffer), stat_file)) {
        fclose(stat_file);
        return 0;
    }
    fclose(stat_file);
    char* cursor = strrchr(buffer, ')');
    if (!cursor || cursor[1] != ' ') return 0;
    cursor += 2;
    unsigned int field = 3;
    char* save = NULL;
    for (char* token = strtok_r(cursor, " ", &save); token; token = strtok_r(NULL, " ", &save), ++field) {
        if (field == 22) {
            errno = 0;
            char* end = NULL;
            unsigned long long value = strtoull(token, &end, 10);
            return errno == 0 && end != token ? value : 0;
        }
    }
    return 0;
}

static void handle_info(int client_fd) {
    char response[256];
    int n = snprintf(response, sizeof(response),
                     "OK protocol=1 pid=%d uid=%d start=%llu features=info,list-hooks,resolve,resolve-token,hook,hook-stats,events\n",
                     (int)getpid(), (int)geteuid(), process_start_ticks());
    if (n > 0 && (size_t)n < sizeof(response)) write_full(client_fd, response, (size_t)n);
}

static void handle_list_hooks(int client_fd) {
    unsigned int active_count = 0;
    for (uint32_t slot = 0; slot < HOOK_REGISTRY_CAPACITY; ++slot) {
        HookHandle handle = { .slot = slot };
        if (hook_registry_get(handle) && g_hook_metadata[slot].known) active_count++;
    }
    char line[1024];
    int n = snprintf(line, sizeof(line), "OK %u\n", active_count);
    if (n > 0) write_full(client_fd, line, (size_t)n);

    for (uint32_t slot = 0; slot < HOOK_REGISTRY_CAPACITY; ++slot) {
        HookHandle handle = { .slot = slot };
        if (!hook_registry_get(handle) || !g_hook_metadata[slot].known) continue;
        HookMetadata* hook = &g_hook_metadata[slot];
        int method_handle = hook->method_handle;
        if (method_handle < 0 || method_handle >= g_method_count) continue;
        int class_handle = g_method_class_handles[method_handle];
        if (class_handle < 0 || class_handle >= g_class_count) continue;
        int image_handle = g_class_image_handles[class_handle];
        if (image_handle < 0 || image_handle >= g_image_count) continue;
        uint64_t hits = 0;
        bool has_hits = hook_probe_hit_count(handle, &hits);
        char hit_text[32];
        if (has_hits) snprintf(hit_text, sizeof(hit_text), "%llu", (unsigned long long)hits);
        else copy_ipc_name(hit_text, sizeof(hit_text), "-");
        const char* separator = g_class_namespaces[class_handle][0] ? "." : "";
        if (g_method_tokens[method_handle] != 0) {
            n = snprintf(line, sizeof(line),
                         "HOOK %u %s %s %s %s!%s%s%s::%s@0x%08x %s\n",
                         slot, hook->mode, hook->transport, hook->handler,
                         g_image_names[image_handle], g_class_namespaces[class_handle], separator,
                         g_class_names[class_handle], g_method_names[method_handle],
                         g_method_tokens[method_handle], hit_text);
        } else {
            n = snprintf(line, sizeof(line),
                         "HOOK %u %s %s %s %s!%s%s%s::%s/%d %s\n",
                         slot, hook->mode, hook->transport, hook->handler,
                         g_image_names[image_handle], g_class_namespaces[class_handle], separator,
                         g_class_names[class_handle], g_method_names[method_handle],
                         g_method_arities[method_handle], hit_text);
        }
        if (n > 0 && (size_t)n < sizeof(line)) write_full(client_fd, line, (size_t)n);
    }
}

static void handle_hook_stats(int client_fd, int argc, const char** args) {
    if (argc < 1) {
        write_full(client_fd, "ERR bad-args\n", 13);
        return;
    }
    long slot;
    if (!parse_nonneg_int(args[0], &slot) || slot > UINT32_MAX) {
        write_full(client_fd, "ERR bad-handle\n", 15);
        return;
    }
    uint64_t count;
    HookHandle handle = { .slot = (uint32_t)slot };
    if (!hook_probe_hit_count(handle, &count)) {
        write_full(client_fd, "ERR not-a-counter-probe\n", 24);
        return;
    }
    char response[64];
    int n = snprintf(response, sizeof(response), "OK %llu\n", (unsigned long long)count);
    if (n > 0) write_full(client_fd, response, (size_t)n);
}

static void handle_events_info(int client_fd) {
    char response[160];
    int n = snprintf(response, sizeof(response),
                     "OK next=%llu oldest=%llu capacity=%u dropped=%llu\n",
                     (unsigned long long)event_stream_next_sequence(),
                     (unsigned long long)event_stream_oldest_sequence(),
                     (unsigned int)IL2BRIDGE_EVENT_CAPACITY,
                     (unsigned long long)event_stream_dropped());
    if (n > 0 && (size_t)n < sizeof(response)) write_full(client_fd, response, (size_t)n);
}

static void handle_events_read(int client_fd, int argc, const char** args) {
    if (argc < 2) {
        write_full(client_fd, "ERR bad-args\n", 13);
        return;
    }
    errno = 0;
    char* end = NULL;
    unsigned long long after = strtoull(args[0], &end, 10);
    if (errno != 0 || end == args[0] || *end != '\0') {
        write_full(client_fd, "ERR bad-sequence\n", 17);
        return;
    }
    long limit;
    if (!parse_nonneg_int(args[1], &limit) || limit < 1 || limit > 256) {
        write_full(client_fd, "ERR bad-limit\n", 14);
        return;
    }
    Il2BridgeEvent events[256];
    size_t count = event_stream_read((uint64_t)after, (size_t)limit, events);
    char line[512];
    int n = snprintf(line, sizeof(line), "OK %zu next=%llu dropped=%llu\n", count,
                     (unsigned long long)event_stream_next_sequence(),
                     (unsigned long long)event_stream_dropped());
    if (n > 0 && (size_t)n < sizeof(line)) write_full(client_fd, line, (size_t)n);
    for (size_t i = 0; i < count; ++i) {
        n = snprintf(line, sizeof(line), "EVENT %llu %llu %u %u %s %s\n",
                     (unsigned long long)events[i].sequence,
                     (unsigned long long)events[i].timestamp_ns,
                     events[i].thread_id, events[i].hook_slot,
                     events[i].type, events[i].payload);
        if (n > 0 && (size_t)n < sizeof(line)) write_full(client_fd, line, (size_t)n);
    }
}

// Detours only increment atomically; the IPC thread publishes aggregate deltas.
static void sample_counter_events(void) {
    for (uint32_t slot = 0; slot < HOOK_REGISTRY_CAPACITY; ++slot) {
        if (!g_hook_metadata[slot].known) continue;
        HookHandle handle = { .slot = slot };
        uint64_t total;
        if (!hook_probe_hit_count(handle, &total)) continue;
        uint64_t previous = g_last_streamed_hits[slot];
        if (total <= previous) continue;
        g_last_streamed_hits[slot] = total;
        char payload[IL2BRIDGE_EVENT_PAYLOAD_SIZE];
        snprintf(payload, sizeof(payload), "delta=%llu,total=%llu",
                 (unsigned long long)(total - previous), (unsigned long long)total);
        event_stream_publish(slot, "hook-hits", payload);
    }
}

static void handle_unhook(int client_fd, int argc, const char** args) {
    if (argc < 1) {
        write_full(client_fd, "ERR bad-args\n", 13);
        return;
    }

    long slot;
    if (!parse_nonneg_int(args[0], &slot) || slot > UINT32_MAX) {
        write_full(client_fd, "ERR bad-handle\n", 15);
        return;
    }

    HookHandle handle;
    handle.slot = (uint32_t)slot;

    const HookEntry* entry = hook_registry_get(handle);
    if (!entry) {
        write_full(client_fd, "ERR bad-handle\n", 15);
        return;
    }

    bool ok = (entry->type == HOOK_TYPE_BREAKPOINT)
        ? hook_uninstall_breakpoint(handle)
        : hook_uninstall_trampoline(handle);

    if (!ok) {
        write_full(client_fd, "ERR unhook-failed\n", 18);
        return;
    }

    event_stream_publish(handle.slot, "hook-removed", "-");
    if (handle.slot < HOOK_REGISTRY_CAPACITY) g_hook_metadata[handle.slot].known = false;

    write_full(client_fd, "OK\n", 3);
}

// Handles one newline-terminated request synchronously. The accept loop sets
// SO_RCVTIMEO and closes the connection after this function returns.
static void handle_connection(int client_fd) {
    sample_counter_events();
    char buf[IPC_LINE_BUFFER_SIZE];
    size_t len = 0;
    bool found_newline = false;

    while (len < sizeof(buf) - 1) {
        ssize_t n = read(client_fd, buf + len, sizeof(buf) - 1 - len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (n == 0) {
            break; // peer closed before sending a newline
        }
        if (memchr(buf + len, '\n', (size_t)n) != NULL) {
            len += (size_t)n;
            found_newline = true;
            break;
        }
        len += (size_t)n;
    }

    if (!found_newline) {
        if (len >= sizeof(buf) - 1) {
            // Never parse a truncated request.
            write_full(client_fd, "ERR line-too-long\n", 18);
        }
        return;
    }

    char* newline = (char*)memchr(buf, '\n', len);
    size_t content_len = (size_t)(newline - buf);
    if (content_len > 0 && buf[content_len - 1] == '\r') {
        content_len--; // tolerate a CRLF-terminated client (see ipc.h)
    }
    buf[content_len] = '\0';

    const char* command = NULL;
    const char* args[IPC_MAX_ARGS];
    int argc = ipc_parse_line(buf, &command, args, IPC_MAX_ARGS);

    if (argc < 0) {
        write_full(client_fd, "ERR bad-request\n", 16);
        return;
    }

    if (strcmp(command, "PING") == 0) {
        write_full(client_fd, "OK PONG\n", 8);
    } else if (strcmp(command, "INFO") == 0) {
        handle_info(client_fd);
    } else if (strcmp(command, "LIST-HOOKS") == 0) {
        handle_list_hooks(client_fd);
    } else if (strcmp(command, "RESOLVE-IMAGE") == 0) {
        handle_resolve_image(client_fd, argc, args);
    } else if (strcmp(command, "RESOLVE-CLASS") == 0) {
        handle_resolve_class(client_fd, argc, args);
    } else if (strcmp(command, "RESOLVE-METHOD") == 0) {
        handle_resolve_method(client_fd, argc, args);
    } else if (strcmp(command, "RESOLVE-METHOD-TOKEN") == 0) {
        handle_resolve_method_token(client_fd, argc, args);
    } else if (strcmp(command, "HOOK") == 0) {
        handle_hook(client_fd, argc, args);
    } else if (strcmp(command, "HOOK-STATS") == 0) {
        handle_hook_stats(client_fd, argc, args);
    } else if (strcmp(command, "EVENTS-INFO") == 0) {
        handle_events_info(client_fd);
    } else if (strcmp(command, "EVENTS-READ") == 0) {
        handle_events_read(client_fd, argc, args);
    } else if (strcmp(command, "UNHOOK") == 0) {
        handle_unhook(client_fd, argc, args);
    } else if (strcmp(command, "CALL") == 0) {
        // Typed argument serialization is not part of protocol v1.
        write_full(client_fd, "ERR not-implemented\n", 20);
    } else {
        write_full(client_fd, "ERR unknown-command\n", 20);
    }
}

static void* accept_loop(void* arg) {
    IpcServer* server = (IpcServer*)arg;

    for (;;) {
        struct pollfd pfds[2];
        pfds[0].fd = server->listen_fd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        pfds[1].fd = server->stop_pipe[0];
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;

        int ready = poll(pfds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break; // unexpected poll() failure -- stop rather than spin forever
        }

        if (pfds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            break; // ipc_server_stop wrote the wake-up byte
        }

        if (pfds[0].revents & POLLIN) {
            int client_fd = accept(server->listen_fd, NULL, NULL);
            if (client_fd < 0) {
                // Keep serving after transient accept failures.
                continue;
            }

            // Drop clients for which the receive bound cannot be installed.
            struct {
                pid_t pid;
                uid_t uid;
                gid_t gid;
            } peer_credentials;
            socklen_t peer_credentials_size = sizeof(peer_credentials);
            if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED,
                           &peer_credentials, &peer_credentials_size) != 0 ||
                peer_credentials.uid != geteuid()) {
                close(client_fd);
                continue;
            }

            struct timeval recv_timeout;
            recv_timeout.tv_sec = IPC_CONNECTION_RECV_TIMEOUT_SEC;
            recv_timeout.tv_usec = 0;
            if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout)) == 0) {
                handle_connection(client_fd);
            }
            close(client_fd);
        }
    }

    return NULL;
}

static bool bind_or_reclaim_stale_socket(int fd, const struct sockaddr_un* addr, const char* path) {
    if (bind(fd, (const struct sockaddr*)addr, sizeof(*addr)) == 0) {
        return true;
    }
    if (errno != EADDRINUSE) {
        return false;
    }

    // Remove only socket nodes that no process accepts connections on.
    int probe = socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0) {
        return false;
    }
    int connected = connect(probe, (const struct sockaddr*)addr, sizeof(*addr));
    int connect_error = errno;
    close(probe);
    if (connected == 0) {
        return false;
    }
    if (connect_error != ECONNREFUSED && connect_error != ENOENT) {
        return false;
    }

    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT && bind(fd, (const struct sockaddr*)addr, sizeof(*addr)) == 0;
    }
    if (!S_ISSOCK(st.st_mode) || st.st_uid != geteuid()) {
        return false;
    }
    if (unlink(path) != 0) {
        return false;
    }

    il2bridge_log("removed orphaned IPC socket %s", path);
    return bind(fd, (const struct sockaddr*)addr, sizeof(*addr)) == 0;
}

IpcServer* ipc_server_start(void) {
    pthread_mutex_lock(&g_server_lifecycle_mutex);
    IpcServer* server = (IpcServer*)calloc(1, sizeof(IpcServer));
    if (!server) {
        pthread_mutex_unlock(&g_server_lifecycle_mutex);
        return NULL;
    }
    server->listen_fd = -1;
    server->stop_pipe[0] = -1;
    server->stop_pipe[1] = -1;

    if (!prepare_runtime_socket_path(getpid(), server->socket_path, sizeof(server->socket_path))) {
        il2bridge_log("could not prepare IPC socket path: %s", strerror(errno));
        free(server);
        pthread_mutex_unlock(&g_server_lifecycle_mutex);
        return NULL;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        il2bridge_log("could not create IPC socket: %s", strerror(errno));
        free(server);
        pthread_mutex_unlock(&g_server_lifecycle_mutex);
        return NULL;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, server->socket_path, strlen(server->socket_path) + 1);

    if (!bind_or_reclaim_stale_socket(fd, &addr, server->socket_path)) {
        il2bridge_log("could not bind IPC socket %s: %s",
                      server->socket_path, strerror(errno));
        close(fd);
        free(server);
        pthread_mutex_unlock(&g_server_lifecycle_mutex);
        return NULL;
    }

    if (chmod(server->socket_path, S_IRUSR | S_IWUSR) != 0) {
        il2bridge_log("could not secure IPC socket %s: %s",
                      server->socket_path, strerror(errno));
        close(fd);
        unlink(server->socket_path);
        free(server);
        pthread_mutex_unlock(&g_server_lifecycle_mutex);
        return NULL;
    }

    if (listen(fd, IPC_LISTEN_BACKLOG) != 0) {
        close(fd);
        // bind() created the path; clean it up on later startup failure.
        unlink(server->socket_path);
        free(server);
        pthread_mutex_unlock(&g_server_lifecycle_mutex);
        return NULL;
    }

    if (pipe(server->stop_pipe) != 0) {
        close(fd);
        unlink(server->socket_path);
        free(server);
        pthread_mutex_unlock(&g_server_lifecycle_mutex);
        return NULL;
    }

    server->listen_fd = fd;

    int rc = pthread_create(&server->thread, NULL, accept_loop, server);
    if (rc != 0) {
        close(server->listen_fd);
        close(server->stop_pipe[0]);
        close(server->stop_pipe[1]);
        unlink(server->socket_path);
        free(server);
        pthread_mutex_unlock(&g_server_lifecycle_mutex);
        return NULL;
    }

    pthread_mutex_unlock(&g_server_lifecycle_mutex);
    return server;
}

void ipc_server_stop(IpcServer* server) {
    if (!server) {
        return;
    }

    pthread_mutex_lock(&g_server_lifecycle_mutex);

    // Wake the accept loop through its self-pipe before joining it.
    char byte = 0;
    ssize_t written;
    do {
        written = write(server->stop_pipe[1], &byte, 1);
    } while (written < 0 && errno == EINTR);

    pthread_join(server->thread, NULL);

    close(server->listen_fd);
    close(server->stop_pipe[0]);
    close(server->stop_pipe[1]);

    unlink(server->socket_path);

    free(server);
    pthread_mutex_unlock(&g_server_lifecycle_mutex);
}
