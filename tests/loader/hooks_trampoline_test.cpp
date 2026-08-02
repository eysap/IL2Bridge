#include "il2bridge/loader/hooks.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace {

#if defined(__clang__)
#define IL2BRIDGE_TEST_HOOK_TARGET __attribute__((noinline, optnone, no_sanitize("address", "undefined")))
#elif defined(__GNUC__)
#define IL2BRIDGE_TEST_HOOK_TARGET __attribute__((noinline, optimize("O0")))
#else
#define IL2BRIDGE_TEST_HOOK_TARGET
#endif

volatile int g_target_ran = 0;
volatile int g_detour_ran = 0;
void (*g_original_via_trampoline)(void) = nullptr;

// Real hook targets are external to the instrumented test binary. Keep this
// synthetic prologue sanitizer-free and explicitly relocatable.
extern "C" IL2BRIDGE_TEST_HOOK_TARGET void trampoline_test_target(void) {
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop; "
                     "nop; nop; nop; nop; nop; nop; nop; nop");
    g_target_ran = g_target_ran + 1;
}

extern "C" void trampoline_test_detour(void) {
    g_detour_ran = g_detour_ran + 1;
    if (g_original_via_trampoline) {
        g_original_via_trampoline();
    }
}

volatile int g_target_b_ran = 0;
volatile int g_detour_b_ran = 0;

extern "C" IL2BRIDGE_TEST_HOOK_TARGET void trampoline_test_target_b(void) {
    __asm__ volatile("nop; nop; nop; nop; nop; nop; nop; nop; "
                     "nop; nop; nop; nop; nop; nop; nop; nop");
    g_target_b_ran = g_target_b_ran + 1;
}

extern "C" void trampoline_test_detour_b(void) {
    g_detour_b_ran = g_detour_b_ran + 1;
}

// RAII storage for hand-assembled relocation fixtures.
struct ExecutableBuffer {
    void* addr = nullptr;
    size_t len = 0;

    explicit ExecutableBuffer(const unsigned char* bytes, size_t n) {
        long page_size = sysconf(_SC_PAGESIZE);
        len = (n + (size_t)page_size - 1) & ~((size_t)page_size - 1);
        addr = mmap(nullptr, len, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        REQUIRE(addr != MAP_FAILED);
        std::memcpy(addr, bytes, n);
    }

    ~ExecutableBuffer() {
        if (addr) munmap(addr, len);
    }
};

} // namespace

TEST_CASE("installing a trampoline hook redirects execution and preserves a callable original", "[hooks_trampoline]") {
    g_target_ran = 0;
    g_detour_ran = 0;

    HookHandle handle{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle));

    void* trampoline = hook_manager_get_trampoline(handle);
    REQUIRE(trampoline != nullptr);
    g_original_via_trampoline = (void (*)(void))trampoline;

    trampoline_test_target();

    REQUIRE(g_detour_ran == 1);
    REQUIRE(g_target_ran == 1); // called through the trampoline from inside the detour

    REQUIRE(hook_uninstall_trampoline(handle));
    g_original_via_trampoline = nullptr;
}

TEST_CASE("after uninstall, the target runs normally without the detour firing", "[hooks_trampoline]") {
    g_target_ran = 0;
    g_detour_ran = 0;

    HookHandle handle{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle));
    REQUIRE(hook_uninstall_trampoline(handle));

    trampoline_test_target();

    REQUIRE(g_target_ran == 1);
    REQUIRE(g_detour_ran == 0);
}

TEST_CASE("an around counter probe observes calls and preserves the original", "[hooks_trampoline]") {
    g_target_ran = 0;
    HookHandle handle{};
    REQUIRE(hook_install_counter_probe((void*)trampoline_test_target, &handle));

    trampoline_test_target();
    trampoline_test_target();

    uint64_t count = 0;
    REQUIRE(hook_probe_hit_count(handle, &count));
    REQUIRE(count == 2);
    REQUIRE(g_target_ran == 2);

    REQUIRE(hook_uninstall_trampoline(handle));
    REQUIRE_FALSE(hook_probe_hit_count(handle, &count));
}

TEST_CASE("two independently installed trampoline hooks coexist without interfering", "[hooks_trampoline]") {
    g_target_ran = 0;
    g_detour_ran = 0;
    g_target_b_ran = 0;
    g_detour_b_ran = 0;

    HookHandle handle_a{};
    HookHandle handle_b{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle_a));
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target_b, (void*)trampoline_test_detour_b, &handle_b));

    trampoline_test_target();
    trampoline_test_target_b();

    REQUIRE(g_detour_ran == 1);
    REQUIRE(g_detour_b_ran == 1);

    REQUIRE(hook_uninstall_trampoline(handle_a));

    trampoline_test_target();
    trampoline_test_target_b();

    REQUIRE(g_target_ran == 1); // A no longer hooked, runs unhooked
    REQUIRE(g_detour_ran == 1); // unchanged
    REQUIRE(g_detour_b_ran == 2); // B fired again

    REQUIRE(hook_uninstall_trampoline(handle_b));
}

TEST_CASE("installing a second trampoline hook on an already-hooked target is rejected", "[hooks_trampoline]") {
    HookHandle handle_a{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle_a));

    HookHandle handle_dup{};
    REQUIRE_FALSE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour_b, &handle_dup));

    REQUIRE(hook_uninstall_trampoline(handle_a));

    HookHandle handle_again{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle_again));
    REQUIRE(hook_uninstall_trampoline(handle_again));
}

TEST_CASE("a relative jump whose destination lands inside the patch window is rejected, not mis-relocated", "[hooks_trampoline]") {
    // rel32 at offset 0 lands at offset 6, inside the 14-byte patch window.
    static const unsigned char code[] = {
        0xE9, 0x01, 0x00, 0x00, 0x00,
        0x90,
        0x90,
        0xB8, 0x2A, 0x00, 0x00, 0x00,
        0x31, 0xC9,
        0xC3,
    };
    ExecutableBuffer buffer(code, sizeof(code));

    void* dummy_detour = (void*)trampoline_test_detour_b;
    HookHandle handle{};
    REQUIRE_FALSE(hook_install_trampoline(buffer.addr, dummy_detour, &handle));
}
