#include "il2bridge/loader/bridge.h"
#include <catch2/catch_test_macros.hpp>
#include <dlfcn.h>
#include <string>

namespace {

// Path to the fake_gameassembly shared library built alongside the tests.
// CMake defines FAKE_GAMEASSEMBLY_PATH via target_compile_definitions.
void* open_fake_gameassembly() {
    void* handle = dlopen(FAKE_GAMEASSEMBLY_PATH, RTLD_NOW);
    return handle;
}

void* open_core_only_gameassembly() {
    return dlopen(CORE_ONLY_GAMEASSEMBLY_PATH, RTLD_NOW);
}

// REQUIRE() throws on failure, so a plain dlclose() placed after it never
// runs and leaks the handle. Wrapping the handle in a scope guard ensures
// it's closed on every exit path, including an assertion failure.
struct DlHandleGuard {
    void* handle;
    ~DlHandleGuard() {
        if (handle) dlclose(handle);
    }
};

} // namespace

TEST_CASE("bridge_init succeeds when every required symbol is present", "[bridge]") {
    DlHandleGuard guard{open_fake_gameassembly()};
    REQUIRE(guard.handle != nullptr);
    REQUIRE(bridge_init(guard.handle));
}

TEST_CASE("optional capabilities resolve independently when available", "[bridge]") {
    DlHandleGuard guard{open_fake_gameassembly()};
    REQUIRE(guard.handle != nullptr);
    REQUIRE(bridge_init(guard.handle));

    REQUIRE(bridge_require_capability(BRIDGE_CAPABILITY_INTROSPECTION));
    REQUIRE(bridge_require_capability(BRIDGE_CAPABILITY_INVOCATION));
    REQUIRE(bridge_require_capability(BRIDGE_CAPABILITY_STRINGS));
    REQUIRE(bridge_require_capability(BRIDGE_CAPABILITY_THREADS));
}

TEST_CASE("missing optional capabilities do not invalidate the core bridge", "[bridge]") {
    DlHandleGuard guard{open_core_only_gameassembly()};
    REQUIRE(guard.handle != nullptr);
    REQUIRE(bridge_init(guard.handle));

    REQUIRE_FALSE(bridge_require_capability(BRIDGE_CAPABILITY_INTROSPECTION));
    REQUIRE_FALSE(bridge_require_capability(BRIDGE_CAPABILITY_INVOCATION));
    REQUIRE_FALSE(bridge_require_capability(BRIDGE_CAPABILITY_STRINGS));
    REQUIRE_FALSE(bridge_require_capability(BRIDGE_CAPABILITY_THREADS));

    REQUIRE(bridge_find_image("Fake.dll") != nullptr);
}

TEST_CASE("invalid capability identifiers fail cleanly", "[bridge]") {
    REQUIRE_FALSE(bridge_require_capability(static_cast<BridgeCapability>(-1)));
    REQUIRE_FALSE(bridge_require_capability(BRIDGE_CAPABILITY_COUNT));
}

TEST_CASE("bounded object and UTF-16 string introspection uses optional capabilities", "[bridge]") {
    DlHandleGuard guard{open_fake_gameassembly()};
    REQUIRE(guard.handle != nullptr);
    REQUIRE(bridge_init(guard.handle));
    auto get_object = reinterpret_cast<Il2CppObject* (*)()>(
        dlsym(guard.handle, "fake_gameassembly_object"));
    auto get_string = reinterpret_cast<Il2CppString* (*)()>(
        dlsym(guard.handle, "fake_gameassembly_string"));
    REQUIRE(get_object != nullptr);
    REQUIRE(get_string != nullptr);

    char type_name[64];
    REQUIRE(bridge_describe_object(get_object(), type_name, sizeof(type_name)));
    REQUIRE(std::string(type_name) == "FakeNamespace.FakeClass");

    char value[64];
    REQUIRE(bridge_string_to_utf8(get_string(), value, sizeof(value)));
    REQUIRE(std::string(value) == "H\xC3\xA9\xF0\x9F\x9A\x80");

    char too_small[2];
    REQUIRE_FALSE(bridge_string_to_utf8(get_string(), too_small, sizeof(too_small)));
    REQUIRE_FALSE(bridge_describe_object(nullptr, type_name, sizeof(type_name)));
}

TEST_CASE("bridge_init fails on a module missing a required symbol", "[bridge]") {
    // libc definitely doesn't export any il2cpp_* symbols.
    DlHandleGuard guard{dlopen("libc.so.6", RTLD_NOW)};
    REQUIRE(guard.handle != nullptr);
    REQUIRE_FALSE(bridge_init(guard.handle));
}

TEST_CASE("resolves the fake image, class, and method end-to-end", "[bridge]") {
    DlHandleGuard guard{open_fake_gameassembly()};
    REQUIRE(guard.handle != nullptr);
    REQUIRE(bridge_init(guard.handle));

    REQUIRE(bridge_find_image("Other.dll") != nullptr);
    REQUIRE(bridge_find_image("Missing.dll") == nullptr);
    REQUIRE(bridge_find_image(nullptr) == nullptr);
    Il2CppImage* image = bridge_find_image("Fake.dll");
    REQUIRE(image != nullptr);

    Il2CppClass* klass = bridge_find_class(image, "FakeNamespace", "FakeClass");
    REQUIRE(klass != nullptr);

    Il2CppClass* missing = bridge_find_class(image, "Wrong", "Namespace");
    REQUIRE(missing == nullptr);

    Il2CppMethod* method = bridge_find_method(klass, "FakeMethod", 0);
    REQUIRE(method != nullptr);
    REQUIRE(bridge_method_pointer(method) != nullptr);
    REQUIRE(bridge_method_token(method) == 0x06000001u);
    REQUIRE(bridge_find_method_by_token(klass, 0x06000001u) == method);
    REQUIRE(bridge_find_method_by_token(klass, 0x06000002u) == nullptr);

    Il2CppMethod* wrong_arity = bridge_find_method(klass, "FakeMethod", 1);
    REQUIRE(wrong_arity == nullptr);
}

TEST_CASE("resolution cache avoids re-resolving on repeated lookups", "[bridge]") {
    DlHandleGuard guard{open_fake_gameassembly()};
    REQUIRE(guard.handle != nullptr);
    REQUIRE(bridge_init(guard.handle));

    // The fixture counter distinguishes a cache hit from identical lookups.
    auto class_from_name_call_count = reinterpret_cast<int (*)()>(
        dlsym(guard.handle, "fake_gameassembly_class_from_name_call_count"));
    REQUIRE(class_from_name_call_count != nullptr);

    Il2CppImage* image = bridge_find_image("Fake.dll");
    REQUIRE(image != nullptr);

    Il2CppClass* first = bridge_find_class(image, "FakeNamespace", "FakeClass");
    REQUIRE(first != nullptr);
    int count_after_first = class_from_name_call_count();

    Il2CppClass* second = bridge_find_class(image, "FakeNamespace", "FakeClass");
    REQUIRE(second == first);
    int count_after_second = class_from_name_call_count();

    REQUIRE(count_after_second == count_after_first);
}
