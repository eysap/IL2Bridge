#include "il2bridge/loader/hooks.h"
#include <catch2/catch_test_macros.hpp>
#include <csignal>

namespace {

volatile int g_target_calls = 0;
volatile int g_detour_calls = 0;

extern "C" __attribute__((noinline)) void breakpoint_test_target(void) {
    g_target_calls = g_target_calls + 1;
}

extern "C" void breakpoint_test_detour(void) {
    g_detour_calls = g_detour_calls + 1;
}

volatile int g_target_b_calls = 0;
volatile int g_detour_b_calls = 0;

extern "C" __attribute__((noinline)) void breakpoint_test_target_b(void) {
    g_target_b_calls = g_target_b_calls + 1;
}

extern "C" void breakpoint_test_detour_b(void) {
    g_detour_b_calls = g_detour_b_calls + 1;
}

} // namespace

TEST_CASE("installing a breakpoint hook redirects execution to the detour", "[hooks_breakpoint]") {
    g_target_calls = 0;
    g_detour_calls = 0;

    HookHandle handle{};
    REQUIRE(hook_install_breakpoint((void*)breakpoint_test_target, (void*)breakpoint_test_detour, &handle));

    breakpoint_test_target();

    REQUIRE(g_detour_calls == 1);
    REQUIRE(g_target_calls == 0);

    REQUIRE(hook_uninstall_breakpoint(handle));
}

TEST_CASE("after uninstall, the target runs normally again", "[hooks_breakpoint]") {
    g_target_calls = 0;
    g_detour_calls = 0;

    HookHandle handle{};
    REQUIRE(hook_install_breakpoint((void*)breakpoint_test_target, (void*)breakpoint_test_detour, &handle));
    REQUIRE(hook_uninstall_breakpoint(handle));

    breakpoint_test_target();

    REQUIRE(g_target_calls == 1);
    REQUIRE(g_detour_calls == 0);
}

TEST_CASE("replacement counter reports hits while suppressing a void target", "[hooks_breakpoint]") {
    g_target_calls = 0;
    HookHandle handle{};
    REQUIRE(hook_install_replacement_counter((void*)breakpoint_test_target, &handle));

    breakpoint_test_target();
    breakpoint_test_target();
    uint64_t count = 0;
    REQUIRE(hook_probe_hit_count(handle, &count));
    REQUIRE(count == 2);
    REQUIRE(g_target_calls == 0);

    REQUIRE(hook_uninstall_breakpoint(handle));
    breakpoint_test_target();
    REQUIRE(g_target_calls == 1);
}

TEST_CASE("two independently installed hooks coexist without interfering", "[hooks_breakpoint]") {
    g_target_calls = 0;
    g_detour_calls = 0;
    g_target_b_calls = 0;
    g_detour_b_calls = 0;

    HookHandle handle_a{};
    HookHandle handle_b{};
    REQUIRE(hook_install_breakpoint((void*)breakpoint_test_target, (void*)breakpoint_test_detour, &handle_a));
    REQUIRE(hook_install_breakpoint((void*)breakpoint_test_target_b, (void*)breakpoint_test_detour_b, &handle_b));

    breakpoint_test_target();
    breakpoint_test_target_b();

    REQUIRE(g_detour_calls == 1);
    REQUIRE(g_detour_b_calls == 1);

    // Removing one hook must not affect another target.
    REQUIRE(hook_uninstall_breakpoint(handle_a));

    breakpoint_test_target();
    breakpoint_test_target_b();

    REQUIRE(g_target_calls == 1);
    REQUIRE(g_detour_calls == 1); // unchanged: A no longer hooked
    REQUIRE(g_detour_b_calls == 2); // B fired again

    REQUIRE(hook_uninstall_breakpoint(handle_b));
}

TEST_CASE("installing a second hook on an already-hooked target is rejected", "[hooks_breakpoint]") {
    g_target_calls = 0;
    g_detour_calls = 0;

    HookHandle handle_a{};
    REQUIRE(hook_install_breakpoint((void*)breakpoint_test_target, (void*)breakpoint_test_detour, &handle_a));

    HookHandle handle_dup{};
    REQUIRE_FALSE(hook_install_breakpoint((void*)breakpoint_test_target, (void*)breakpoint_test_detour_b, &handle_dup));

    REQUIRE(hook_uninstall_breakpoint(handle_a));

    HookHandle handle_again{};
    REQUIRE(hook_install_breakpoint((void*)breakpoint_test_target, (void*)breakpoint_test_detour, &handle_again));
    REQUIRE(hook_uninstall_breakpoint(handle_again));
}
