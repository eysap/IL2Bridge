#pragma once
#include "il2bridge/loader/api.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque IL2CPP types. bridge_method_pointer() is the sole internal-layout
// access because IL2CPP exposes no native method-address accessor.
typedef struct Il2CppDomain Il2CppDomain;
typedef struct Il2CppAssembly Il2CppAssembly;
typedef struct Il2CppImage Il2CppImage;
typedef struct Il2CppClass Il2CppClass;
typedef struct Il2CppMethod Il2CppMethod;
typedef struct Il2CppObject Il2CppObject;
typedef struct Il2CppString Il2CppString;
typedef struct Il2CppThread Il2CppThread;

// Optional IL2CPP APIs are resolved by capability. Protocol handlers use
// bridge operations and never resolve IL2CPP symbols directly.
typedef enum {
    BRIDGE_CAPABILITY_INTROSPECTION = 0,
    BRIDGE_CAPABILITY_INVOCATION,
    BRIDGE_CAPABILITY_STRINGS,
    BRIDGE_CAPABILITY_THREADS,
    BRIDGE_CAPABILITY_COUNT,
} BridgeCapability;

// Resolves the required API from an already-mapped module. The function is
// serialized and publishes no partial table; call it before serving requests.
IL2BRIDGE_LOADER_API bool bridge_init(void* module_handle);

// The module handle passed to a successful bridge_init(), or NULL before one.
// A consumer needing its own il2cpp_* access should dlsym() through this handle
// rather than repeating the RTLD_NOLOAD discovery.
IL2BRIDGE_LOADER_API void* bridge_module_handle(void);

// Attaches the calling thread to the IL2CPP runtime so it may touch managed
// memory. Returns NULL if BRIDGE_CAPABILITY_THREADS is unavailable, if the
// domain accessor yields NULL, or if the underlying il2cpp_thread_attach call
// fails. The returned thread must be released with bridge_thread_detach().
IL2BRIDGE_LOADER_API Il2CppThread* bridge_thread_attach(void);

// Releases a thread obtained from bridge_thread_attach(). NULL is ignored.
IL2BRIDGE_LOADER_API void bridge_thread_detach(Il2CppThread* thread);

// Resolves and caches one optional capability without invalidating the core.
// Safe to call concurrently.
IL2BRIDGE_LOADER_API bool bridge_require_capability(BridgeCapability capability);

// Bounded adapters over optional introspection and string capabilities.
IL2BRIDGE_LOADER_API bool bridge_describe_object(Il2CppObject* object, char* out, size_t out_size);
IL2BRIDGE_LOADER_API bool bridge_string_to_utf8(Il2CppString* string, char* out, size_t out_size);

IL2BRIDGE_LOADER_API void* bridge_method_pointer(Il2CppMethod* method);

// Lookup functions share mutex-protected name caches and are thread-safe.

// Resolves an image by its exact assembly filename (for example Sample.dll)
// by enumerating the domain and comparing il2cpp_image_get_name(). Returns
// NULL for an empty/unknown name, or if there is no initialized domain.
IL2BRIDGE_LOADER_API Il2CppImage* bridge_find_image(const char* assembly_name);

// Resolves a class by namespace and name within an image. Results are
// cached by (image, namespace, name); returns NULL if not found.
IL2BRIDGE_LOADER_API Il2CppClass* bridge_find_class(Il2CppImage* image, const char* namespace_name, const char* class_name);

// Resolves a method by name and argument count within a class. Results are
// cached by (class, name, arg_count); returns NULL if not found.
IL2BRIDGE_LOADER_API Il2CppMethod* bridge_find_method(Il2CppClass* klass, const char* method_name, int arg_count);
IL2BRIDGE_LOADER_API Il2CppMethod* bridge_find_method_unique(Il2CppClass* klass, const char* method_name,
                                                             int arg_count, bool* ambiguous);

// Resolves a method by its stable metadata token within a class. Tokens are
// authoritative identities; name/arity remains a human-readable fallback.
IL2BRIDGE_LOADER_API Il2CppMethod* bridge_find_method_by_token(Il2CppClass* klass, unsigned int token);
IL2BRIDGE_LOADER_API unsigned int bridge_method_token(Il2CppMethod* method);

#ifdef __cplusplus
}
#endif
