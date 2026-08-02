// Minimal il2cpp_* ABI fixture for bridge and IPC tests.

#include <stddef.h>
#include <string.h>

typedef struct Il2CppDomain { int dummy; } Il2CppDomain;
typedef struct Il2CppAssembly { int index; } Il2CppAssembly;
typedef struct Il2CppImage { const char* name; } Il2CppImage;
typedef struct Il2CppClass { const char* namespaze; const char* name; } Il2CppClass;
// Keep the native pointer fields first to match bridge.c's MethodInfo prefix.
typedef struct Il2CppMethod { void* methodPointer; void* invokerMethod; const char* name; int arg_count; unsigned int token; } Il2CppMethod;
typedef struct Il2CppObject { int dummy; } Il2CppObject;
typedef struct Il2CppString { int length; unsigned short chars[8]; } Il2CppString;
typedef struct Il2CppThread { int dummy; } Il2CppThread;

#if defined(__clang__)
#define IL2BRIDGE_TEST_HOOK_TARGET __attribute__((noinline, optnone, no_sanitize("address", "undefined")))
#elif defined(__GNUC__)
#define IL2BRIDGE_TEST_HOOK_TARGET __attribute__((noinline, optimize("O0")))
#else
#define IL2BRIDGE_TEST_HOOK_TARGET
#endif

static Il2CppDomain g_domain;
static Il2CppAssembly g_assemblies_storage[2] = { { 0 }, { 1 } };
static const Il2CppAssembly* g_assemblies[2];
static Il2CppImage g_images[2] = { { "Other.dll" }, { "Fake.dll" } };
static Il2CppClass g_class = { "FakeNamespace", "FakeClass" };
#ifndef FAKE_GAMEASSEMBLY_CORE_ONLY
static Il2CppObject g_object;
static Il2CppString g_string = { 4, { 'H', 0x00E9, 0xD83D, 0xDE80 } };
#endif

// Model an external, non-instrumented IL2CPP method with a relocatable prefix
// long enough for the 14-byte trampoline patch in every build configuration.
static volatile int g_fake_method_body_calls;
IL2BRIDGE_TEST_HOOK_TARGET
static void fake_method_body(void) {
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop; "
                     "nop; nop; nop; nop; nop; nop; nop; nop");
    g_fake_method_body_calls++;
    g_fake_method_body_calls++;
    g_fake_method_body_calls--;
}
static Il2CppMethod g_method = { (void*)fake_method_body, NULL, "FakeMethod", 0, 0x06000001u };

void fake_gameassembly_call_method(void) {
    fake_method_body();
}

// Exposed below so tests can distinguish a cache hit from repeated lookup.
static int g_class_from_name_call_count;

Il2CppDomain* il2cpp_domain_get(void) {
    return &g_domain;
}

const Il2CppAssembly** il2cpp_domain_get_assemblies(Il2CppDomain* domain, size_t* size) {
    (void)domain;
    g_assemblies[0] = &g_assemblies_storage[0];
    g_assemblies[1] = &g_assemblies_storage[1];
    *size = 2;
    return g_assemblies;
}

const Il2CppImage* il2cpp_assembly_get_image(const Il2CppAssembly* assembly) {
    return &g_images[assembly->index];
}

const char* il2cpp_image_get_name(const Il2CppImage* image) {
    return image->name;
}

Il2CppClass* il2cpp_class_from_name(const Il2CppImage* image, const char* namesp, const char* name) {
    g_class_from_name_call_count++;
    if (image == &g_images[1] && strcmp(namesp, g_class.namespaze) == 0 && strcmp(name, g_class.name) == 0) {
        return &g_class;
    }
    return NULL;
}

int fake_gameassembly_class_from_name_call_count(void) {
    return g_class_from_name_call_count;
}

Il2CppMethod* il2cpp_class_get_method_from_name(Il2CppClass* klass, const char* name, int argsCount) {
    if (klass == &g_class && strcmp(name, g_method.name) == 0 && argsCount == g_method.arg_count) {
        return &g_method;
    }
    return NULL;
}

const Il2CppMethod* il2cpp_class_get_methods(Il2CppClass* klass, void** iterator) {
    if (klass != &g_class || !iterator || *iterator != NULL) return NULL;
    *iterator = &g_method;
    return &g_method;
}

unsigned int il2cpp_method_get_token(const Il2CppMethod* method) {
    return method ? method->token : 0;
}

const char* il2cpp_method_get_name(const Il2CppMethod* method) {
    return method ? method->name : NULL;
}

unsigned int il2cpp_method_get_param_count(const Il2CppMethod* method) {
    return method ? (unsigned int)method->arg_count : 0;
}

#ifndef FAKE_GAMEASSEMBLY_CORE_ONLY
const char* il2cpp_class_get_name(Il2CppClass* klass) {
    return klass->name;
}

const char* il2cpp_class_get_namespace(Il2CppClass* klass) {
    return klass->namespaze;
}

Il2CppClass* il2cpp_object_get_class(Il2CppObject* obj) {
    return obj == &g_object ? &g_class : NULL;
}

Il2CppObject* il2cpp_runtime_invoke(Il2CppMethod* method, void* obj, void** params, Il2CppObject** exc) {
    (void)method; (void)obj; (void)params; (void)exc;
    return NULL;
}

const unsigned short* il2cpp_string_chars(Il2CppString* str) {
    return str->chars;
}

int il2cpp_string_length(Il2CppString* str) {
    return str->length;
}

Il2CppObject* fake_gameassembly_object(void) { return &g_object; }
Il2CppString* fake_gameassembly_string(void) { return &g_string; }

Il2CppThread* il2cpp_thread_attach(Il2CppDomain* domain) {
    (void)domain;
    return NULL;
}

void il2cpp_thread_detach(Il2CppThread* thread) {
    (void)thread;
}

Il2CppThread* il2cpp_thread_current(void) {
    return NULL;
}
#endif
