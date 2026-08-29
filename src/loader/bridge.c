#include "il2bridge/loader/bridge.h"
#include "il2bridge/loader/log.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef Il2CppDomain* (*il2cpp_domain_get_t)(void);
typedef const Il2CppAssembly** (*il2cpp_domain_get_assemblies_t)(Il2CppDomain*, size_t*);
typedef const Il2CppImage* (*il2cpp_assembly_get_image_t)(const Il2CppAssembly*);
typedef const char* (*il2cpp_image_get_name_t)(const Il2CppImage*);
typedef Il2CppClass* (*il2cpp_class_from_name_t)(const Il2CppImage*, const char*, const char*);
typedef const Il2CppMethod* (*il2cpp_class_get_methods_t)(Il2CppClass*, void**);
typedef uint32_t (*il2cpp_method_get_token_t)(const Il2CppMethod*);
typedef const char* (*il2cpp_method_get_name_t)(const Il2CppMethod*);
typedef uint32_t (*il2cpp_method_get_param_count_t)(const Il2CppMethod*);
typedef const char* (*il2cpp_class_get_name_t)(Il2CppClass*);
typedef const char* (*il2cpp_class_get_namespace_t)(Il2CppClass*);
typedef Il2CppClass* (*il2cpp_object_get_class_t)(Il2CppObject*);
typedef Il2CppObject* (*il2cpp_runtime_invoke_t)(Il2CppMethod*, void*, void**, Il2CppObject**);
typedef const unsigned short* (*il2cpp_string_chars_t)(Il2CppString*);
typedef int (*il2cpp_string_length_t)(Il2CppString*);
typedef Il2CppThread* (*il2cpp_thread_attach_t)(Il2CppDomain*);
typedef void (*il2cpp_thread_detach_t)(Il2CppThread*);
typedef Il2CppThread* (*il2cpp_thread_current_t)(void);

// Internal IL2CPP ABI assumption: MethodInfo begins with methodPointer.
// Unity does not expose a public accessor for the native code address.
typedef struct {
    void* method_pointer;
    void* invoker_method;
} MethodInfoLayout;

static il2cpp_domain_get_t p_domain_get;
static il2cpp_domain_get_assemblies_t p_domain_get_assemblies;
static il2cpp_assembly_get_image_t p_assembly_get_image;
static il2cpp_image_get_name_t p_image_get_name;
static il2cpp_class_from_name_t p_class_from_name;
static il2cpp_class_get_methods_t p_class_get_methods;
static il2cpp_method_get_token_t p_method_get_token;
static il2cpp_method_get_name_t p_method_get_name;
static il2cpp_method_get_param_count_t p_method_get_param_count;
static il2cpp_class_get_name_t p_class_get_name;
static il2cpp_class_get_namespace_t p_class_get_namespace;
static il2cpp_object_get_class_t p_object_get_class;
static il2cpp_runtime_invoke_t p_runtime_invoke;
static il2cpp_string_chars_t p_string_chars;
static il2cpp_string_length_t p_string_length;
static il2cpp_thread_attach_t p_thread_attach;
static il2cpp_thread_detach_t p_thread_detach;
static il2cpp_thread_current_t p_thread_current;
static void* g_module_handle;

typedef enum {
    CAPABILITY_UNKNOWN,
    CAPABILITY_AVAILABLE,
    CAPABILITY_UNAVAILABLE,
} CapabilityState;

static CapabilityState g_capability_states[BRIDGE_CAPABILITY_COUNT];

#define RESOLVE_OR_FAIL(target, handle, symbol_name)               \
    do {                                                            \
        target = (typeof(target))dlsym((handle), (symbol_name));    \
        if (!target) {                                              \
            const char* error = dlerror();                          \
            il2bridge_log("missing required symbol %s: %s",         \
                    (symbol_name),                                  \
                    error ? error : "unknown dlsym error");         \
            return false;                                           \
        }                                                            \
    } while (0)

static bool resolve_all_symbols(void* module_handle) {
    RESOLVE_OR_FAIL(p_domain_get, module_handle, "il2cpp_domain_get");
    RESOLVE_OR_FAIL(p_domain_get_assemblies, module_handle, "il2cpp_domain_get_assemblies");
    RESOLVE_OR_FAIL(p_assembly_get_image, module_handle, "il2cpp_assembly_get_image");
    RESOLVE_OR_FAIL(p_image_get_name, module_handle, "il2cpp_image_get_name");
    RESOLVE_OR_FAIL(p_class_from_name, module_handle, "il2cpp_class_from_name");
    RESOLVE_OR_FAIL(p_class_get_methods, module_handle, "il2cpp_class_get_methods");
    RESOLVE_OR_FAIL(p_method_get_token, module_handle, "il2cpp_method_get_token");
    RESOLVE_OR_FAIL(p_method_get_name, module_handle, "il2cpp_method_get_name");
    RESOLVE_OR_FAIL(p_method_get_param_count, module_handle, "il2cpp_method_get_param_count");

    return true;
}

static pthread_mutex_t g_init_mutex = PTHREAD_MUTEX_INITIALIZER;

#define RESOLVE_OPTIONAL_OR_FAIL(target, symbol_name, capability_name)       \
    do {                                                                      \
        target = (typeof(target))dlsym(g_module_handle, (symbol_name));       \
        if (!target) {                                                        \
            il2bridge_log("capability %s unavailable: missing %s",            \
                    (capability_name), (symbol_name));                         \
            return false;                                                     \
        }                                                                     \
    } while (0)

static bool resolve_introspection(void) {
    RESOLVE_OPTIONAL_OR_FAIL(p_class_get_name, "il2cpp_class_get_name", "introspection");
    RESOLVE_OPTIONAL_OR_FAIL(p_class_get_namespace, "il2cpp_class_get_namespace", "introspection");
    RESOLVE_OPTIONAL_OR_FAIL(p_object_get_class, "il2cpp_object_get_class", "introspection");
    return true;
}

static bool resolve_invocation(void) {
    RESOLVE_OPTIONAL_OR_FAIL(p_runtime_invoke, "il2cpp_runtime_invoke", "invocation");
    return true;
}

static bool resolve_strings(void) {
    RESOLVE_OPTIONAL_OR_FAIL(p_string_chars, "il2cpp_string_chars", "strings");
    RESOLVE_OPTIONAL_OR_FAIL(p_string_length, "il2cpp_string_length", "strings");
    return true;
}

static bool resolve_threads(void) {
    RESOLVE_OPTIONAL_OR_FAIL(p_thread_attach, "il2cpp_thread_attach", "threads");
    RESOLVE_OPTIONAL_OR_FAIL(p_thread_detach, "il2cpp_thread_detach", "threads");
    RESOLVE_OPTIONAL_OR_FAIL(p_thread_current, "il2cpp_thread_current", "threads");
    return true;
}

static void reset_optional_capabilities(void) {
    memset(g_capability_states, 0, sizeof(g_capability_states));
    p_class_get_name = NULL;
    p_class_get_namespace = NULL;
    p_object_get_class = NULL;
    p_runtime_invoke = NULL;
    p_string_chars = NULL;
    p_string_length = NULL;
    p_thread_attach = NULL;
    p_thread_detach = NULL;
    p_thread_current = NULL;
}

static void reset_lookup_caches(void);

// Resolve a complete core API table before publishing it to callers.
bool bridge_init(void* module_handle) {
    if (!module_handle) {
        return false;
    }
    pthread_mutex_lock(&g_init_mutex);
    bool result = resolve_all_symbols(module_handle);
    if (result) {
        g_module_handle = module_handle;
        reset_optional_capabilities();
        reset_lookup_caches();
    }
    pthread_mutex_unlock(&g_init_mutex);
    return result;
}

bool bridge_require_capability(BridgeCapability capability) {
    if (capability < 0 || capability >= BRIDGE_CAPABILITY_COUNT) {
        return false;
    }

    pthread_mutex_lock(&g_init_mutex);
    CapabilityState state = g_capability_states[capability];
    if (state != CAPABILITY_UNKNOWN) {
        pthread_mutex_unlock(&g_init_mutex);
        return state == CAPABILITY_AVAILABLE;
    }
    if (!g_module_handle) {
        pthread_mutex_unlock(&g_init_mutex);
        return false;
    }

    bool available = false;
    switch (capability) {
        case BRIDGE_CAPABILITY_INTROSPECTION:
            available = resolve_introspection();
            break;
        case BRIDGE_CAPABILITY_INVOCATION:
            available = resolve_invocation();
            break;
        case BRIDGE_CAPABILITY_STRINGS:
            available = resolve_strings();
            break;
        case BRIDGE_CAPABILITY_THREADS:
            available = resolve_threads();
            break;
        case BRIDGE_CAPABILITY_COUNT:
            break;
    }

    g_capability_states[capability] = available ? CAPABILITY_AVAILABLE : CAPABILITY_UNAVAILABLE;
    pthread_mutex_unlock(&g_init_mutex);
    return available;
}

bool bridge_describe_object(Il2CppObject* object, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!object || !bridge_require_capability(BRIDGE_CAPABILITY_INTROSPECTION)) return false;
    Il2CppClass* klass = p_object_get_class(object);
    if (!klass) return false;
    const char* name = p_class_get_name(klass);
    const char* namespaze = p_class_get_namespace(klass);
    if (!name) return false;
    int written = (namespaze && namespaze[0] != '\0')
        ? snprintf(out, out_size, "%s.%s", namespaze, name)
        : snprintf(out, out_size, "%s", name);
    return written >= 0 && (size_t)written < out_size;
}

static bool append_utf8(char* out, size_t out_size, size_t* cursor, uint32_t codepoint) {
    unsigned char bytes[4];
    size_t count;
    if (codepoint <= 0x7F) {
        bytes[0] = (unsigned char)codepoint; count = 1;
    } else if (codepoint <= 0x7FF) {
        bytes[0] = 0xC0 | (unsigned char)(codepoint >> 6);
        bytes[1] = 0x80 | (unsigned char)(codepoint & 0x3F); count = 2;
    } else if (codepoint <= 0xFFFF) {
        bytes[0] = 0xE0 | (unsigned char)(codepoint >> 12);
        bytes[1] = 0x80 | (unsigned char)((codepoint >> 6) & 0x3F);
        bytes[2] = 0x80 | (unsigned char)(codepoint & 0x3F); count = 3;
    } else {
        bytes[0] = 0xF0 | (unsigned char)(codepoint >> 18);
        bytes[1] = 0x80 | (unsigned char)((codepoint >> 12) & 0x3F);
        bytes[2] = 0x80 | (unsigned char)((codepoint >> 6) & 0x3F);
        bytes[3] = 0x80 | (unsigned char)(codepoint & 0x3F); count = 4;
    }
    if (*cursor + count >= out_size) return false;
    memcpy(out + *cursor, bytes, count);
    *cursor += count;
    out[*cursor] = '\0';
    return true;
}

bool bridge_string_to_utf8(Il2CppString* string, char* out, size_t out_size) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    if (!string || !bridge_require_capability(BRIDGE_CAPABILITY_STRINGS)) return false;
    int length = p_string_length(string);
    const unsigned short* chars = p_string_chars(string);
    if (length < 0 || (length > 0 && !chars)) return false;
    size_t cursor = 0;
    for (int i = 0; i < length; ++i) {
        uint32_t codepoint = chars[i];
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (i + 1 < length && chars[i + 1] >= 0xDC00 && chars[i + 1] <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (chars[++i] - 0xDC00);
            } else {
                codepoint = 0xFFFD;
            }
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            codepoint = 0xFFFD;
        }
        if (!append_utf8(out, out_size, &cursor, codepoint)) return false;
    }
    return true;
}

void* bridge_method_pointer(Il2CppMethod* method) {
    if (!method) {
        return NULL;
    }
    return ((MethodInfoLayout*)method)->method_pointer;
}

// Fixed-capacity lookup caches, guarded by g_cache_mutex.
typedef struct {
    char key[256];
    void* value;
} CacheEntry;

#define CACHE_CAPACITY 256

static CacheEntry g_image_cache[CACHE_CAPACITY];
static size_t g_image_cache_count;
static CacheEntry g_class_cache[CACHE_CAPACITY];
static size_t g_class_cache_count;
static CacheEntry g_method_cache[CACHE_CAPACITY];
static size_t g_method_cache_count;

static void* cache_lookup(CacheEntry* cache, size_t count, const char* key) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(cache[i].key, key) == 0) {
            return cache[i].value;
        }
    }
    return NULL;
}

static void cache_insert(CacheEntry* cache, size_t* count, const char* key, void* value) {
    if (*count >= CACHE_CAPACITY) {
        return; // cache full: skip caching, correctness unaffected, just no speedup for this entry
    }
    strncpy(cache[*count].key, key, sizeof(cache[*count].key) - 1);
    cache[*count].key[sizeof(cache[*count].key) - 1] = '\0';
    cache[*count].value = value;
    (*count)++;
}

static pthread_mutex_t g_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static void reset_lookup_caches(void) {
    pthread_mutex_lock(&g_cache_mutex);
    memset(g_image_cache, 0, sizeof(g_image_cache));
    memset(g_class_cache, 0, sizeof(g_class_cache));
    memset(g_method_cache, 0, sizeof(g_method_cache));
    g_image_cache_count = 0;
    g_class_cache_count = 0;
    g_method_cache_count = 0;
    pthread_mutex_unlock(&g_cache_mutex);
}

Il2CppImage* bridge_find_image(const char* assembly_name) {
    // Fail closed if called before the core symbol table is available.
    if (!assembly_name || assembly_name[0] == '\0' || !p_domain_get ||
        !p_domain_get_assemblies || !p_assembly_get_image || !p_image_get_name) {
        return NULL;
    }

    const char* key = assembly_name;

    pthread_mutex_lock(&g_cache_mutex);
    void* cached = cache_lookup(g_image_cache, g_image_cache_count, key);
    pthread_mutex_unlock(&g_cache_mutex);
    if (cached) {
        return (Il2CppImage*)cached;
    }

    Il2CppDomain* domain = p_domain_get();
    if (!domain) {
        return NULL;
    }

    size_t count = 0;
    const Il2CppAssembly** assemblies = p_domain_get_assemblies(domain, &count);
    if (!assemblies || count == 0) {
        return NULL;
    }

    for (size_t i = 0; i < count; ++i) {
        const Il2CppImage* image = p_assembly_get_image(assemblies[i]);
        const char* image_name = image ? p_image_get_name(image) : NULL;
        if (image_name && strcmp(image_name, assembly_name) == 0) {
            pthread_mutex_lock(&g_cache_mutex);
            cache_insert(g_image_cache, &g_image_cache_count, key, (void*)image);
            pthread_mutex_unlock(&g_cache_mutex);
            return (Il2CppImage*)image;
        }
    }
    return NULL;
}

// Length-prefix variable components so distinct tuples cannot alias.
static bool format_two_part_key(char* key, size_t key_size, const void* ptr, const char* a, const char* b) {
    int written = snprintf(key, key_size, "%p:%zu:%s:%zu:%s", ptr, strlen(a), a, strlen(b), b);
    return written > 0 && (size_t)written < key_size;
}

Il2CppClass* bridge_find_class(Il2CppImage* image, const char* namespace_name, const char* class_name) {
    if (!image || !namespace_name || !class_name) {
        return NULL;
    }
    if (!p_class_from_name) {
        return NULL;
    }

    char key[256];
    bool key_ok = format_two_part_key(key, sizeof(key), (void*)image, namespace_name, class_name);

    pthread_mutex_lock(&g_cache_mutex);
    void* cached = key_ok ? cache_lookup(g_class_cache, g_class_cache_count, key) : NULL;
    pthread_mutex_unlock(&g_cache_mutex);
    if (cached) {
        return (Il2CppClass*)cached;
    }

    Il2CppClass* klass = p_class_from_name(image, namespace_name, class_name);
    if (klass && key_ok) {
        pthread_mutex_lock(&g_cache_mutex);
        cache_insert(g_class_cache, &g_class_cache_count, key, klass);
        pthread_mutex_unlock(&g_cache_mutex);
    }
    return klass;
}

Il2CppMethod* bridge_find_method(Il2CppClass* klass, const char* method_name, int arg_count) {
    return bridge_find_method_unique(klass, method_name, arg_count, NULL);
}

Il2CppMethod* bridge_find_method_unique(Il2CppClass* klass, const char* method_name, int arg_count,
                                        bool* ambiguous) {
    if (ambiguous) *ambiguous = false;
    if (!klass || !method_name) {
        return NULL;
    }
    if (!p_class_get_methods || !p_method_get_name || !p_method_get_param_count) {
        return NULL;
    }

    char arg_count_str[16];
    snprintf(arg_count_str, sizeof(arg_count_str), "%d", arg_count);

    char key[256];
    bool key_ok = format_two_part_key(key, sizeof(key), (void*)klass, method_name, arg_count_str);

    pthread_mutex_lock(&g_cache_mutex);
    void* cached = key_ok ? cache_lookup(g_method_cache, g_method_cache_count, key) : NULL;
    pthread_mutex_unlock(&g_cache_mutex);
    if (cached) {
        return (Il2CppMethod*)cached;
    }

    Il2CppMethod* method = NULL;
    void* iterator = NULL;
    const Il2CppMethod* candidate;
    while ((candidate = p_class_get_methods(klass, &iterator)) != NULL) {
        const char* candidate_name = p_method_get_name(candidate);
        if (!candidate_name || strcmp(candidate_name, method_name) != 0 ||
            p_method_get_param_count(candidate) != (uint32_t)arg_count) continue;
        if (method) {
            if (ambiguous) *ambiguous = true;
            return NULL;
        }
        method = (Il2CppMethod*)candidate;
    }
    if (method && key_ok) {
        pthread_mutex_lock(&g_cache_mutex);
        cache_insert(g_method_cache, &g_method_cache_count, key, method);
        pthread_mutex_unlock(&g_cache_mutex);
    }
    return method;
}

unsigned int bridge_method_token(Il2CppMethod* method) {
    return method && p_method_get_token ? p_method_get_token(method) : 0;
}

Il2CppMethod* bridge_find_method_by_token(Il2CppClass* klass, unsigned int token) {
    if (!klass || token == 0 || !p_class_get_methods || !p_method_get_token) return NULL;

    char token_text[16];
    snprintf(token_text, sizeof(token_text), "%08x", token);
    char key[256];
    bool key_ok = format_two_part_key(key, sizeof(key), (void*)klass, "token", token_text);
    pthread_mutex_lock(&g_cache_mutex);
    void* cached = key_ok ? cache_lookup(g_method_cache, g_method_cache_count, key) : NULL;
    pthread_mutex_unlock(&g_cache_mutex);
    if (cached) return (Il2CppMethod*)cached;

    void* iterator = NULL;
    const Il2CppMethod* method;
    while ((method = p_class_get_methods(klass, &iterator)) != NULL) {
        if (p_method_get_token(method) != token) continue;
        if (key_ok) {
            pthread_mutex_lock(&g_cache_mutex);
            cache_insert(g_method_cache, &g_method_cache_count, key, (void*)method);
            pthread_mutex_unlock(&g_cache_mutex);
        }
        return (Il2CppMethod*)method;
    }
    return NULL;
}
