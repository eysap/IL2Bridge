#include "il2bridge/loader/hooks.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
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

// Builds an executable fixture that is callable as int(*)(void).
using IntFn = int (*)();

// A prologue whose whole-instruction boundary lands at 17 bytes: six pushes
// (10 bytes) then `sub rsp, 0x88`, which needs an imm32 and so occupies 7.
// This is a real IL2CPP method shape, not a synthetic worst case.
const unsigned char kPrologue17[] = {
    0x55,                                     // push rbp
    0x41, 0x57,                               // push r15
    0x41, 0x56,                               // push r14
    0x41, 0x55,                               // push r13
    0x41, 0x54,                               // push r12
    0x53,                                     // push rbx
    0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, // sub rsp, 0x88   -> boundary at 17
    0x48, 0x81, 0xC4, 0x88, 0x00, 0x00, 0x00, // add rsp, 0x88
    0x5B,                                     // pop rbx
    0x41, 0x5C,                               // pop r12
    0x41, 0x5D,                               // pop r13
    0x41, 0x5E,                               // pop r14
    0x41, 0x5F,                               // pop r15
    0x5D,                                     // pop rbp
    0xB8, 0x2A, 0x00, 0x00, 0x00,             // mov eax, 42
    0xC3,                                     // ret
};

volatile int g_prologue17_detour_ran = 0;
IntFn g_prologue17_original = nullptr;

extern "C" int prologue17_detour(void) {
    g_prologue17_detour_ran = g_prologue17_detour_ran + 1;
    return g_prologue17_original ? g_prologue17_original() : -1;
}

} // namespace

TEST_CASE("installing a trampoline hook redirects execution and preserves a callable original", "[hooks_trampoline]") {
    g_target_ran = 0;
    g_detour_ran = 0;

    HookHandle handle{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle, nullptr));

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
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle, nullptr));
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
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle_a, nullptr));
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target_b, (void*)trampoline_test_detour_b, &handle_b, nullptr));

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
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle_a, nullptr));

    HookHandle handle_dup{};
    REQUIRE_FALSE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour_b, &handle_dup, nullptr));

    REQUIRE(hook_uninstall_trampoline(handle_a));

    HookHandle handle_again{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target, (void*)trampoline_test_detour, &handle_again, nullptr));
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
    REQUIRE_FALSE(hook_install_trampoline(buffer.addr, dummy_detour, &handle, nullptr));
}


// --- API guards -------------------------------------------------------------

TEST_CASE("install rejects null arguments", "[hooks_trampoline]") {
    HookHandle handle{};

    REQUIRE_FALSE(hook_install_trampoline(nullptr, (void*)trampoline_test_detour, &handle, nullptr));
    REQUIRE_FALSE(hook_install_trampoline((void*)trampoline_test_target, nullptr, &handle, nullptr));
    REQUIRE_FALSE(hook_install_trampoline((void*)trampoline_test_target,
                                          (void*)trampoline_test_detour, nullptr, nullptr));
    REQUIRE_FALSE(hook_install_counter_probe(nullptr, &handle));
    REQUIRE_FALSE(hook_install_counter_probe((void*)trampoline_test_target, nullptr));
}

TEST_CASE("hook_probe_hit_count rejects a null out parameter", "[hooks_trampoline]") {
    HookHandle handle{};
    REQUIRE(hook_install_counter_probe((void*)trampoline_test_target, &handle));

    REQUIRE_FALSE(hook_probe_hit_count(handle, nullptr));

    REQUIRE(hook_uninstall_trampoline(handle));
}

TEST_CASE("a replace hook carries no probe counter", "[hooks_trampoline]") {
    HookHandle handle{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target,
                                    (void*)trampoline_test_detour, &handle, nullptr));

    uint64_t count = 0;
    REQUIRE_FALSE(hook_probe_hit_count(handle, &count));

    REQUIRE(hook_uninstall_trampoline(handle));
}

TEST_CASE("uninstalling twice fails the second time", "[hooks_trampoline]") {
    HookHandle handle{};
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target,
                                    (void*)trampoline_test_detour, &handle, nullptr));

    REQUIRE(hook_uninstall_trampoline(handle));
    REQUIRE_FALSE(hook_uninstall_trampoline(handle));
    REQUIRE(hook_manager_get_trampoline(handle) == nullptr);
}

// --- prologue decoding ------------------------------------------------------

TEST_CASE("a prologue that fails to decode is rejected", "[hooks_trampoline]") {
    // 0x06 (PUSH ES) is invalid in 64-bit mode, so no whole-instruction patch
    // length can be computed.
    static const unsigned char code[16] = {
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
        0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
    };
    ExecutableBuffer buffer(code, sizeof(code));

    HookHandle handle{};
    REQUIRE_FALSE(hook_install_trampoline(buffer.addr, (void*)trampoline_test_detour_b, &handle, nullptr));
    REQUIRE_FALSE(hook_install_counter_probe(buffer.addr, &handle));
}

// --- relocation -------------------------------------------------------------

TEST_CASE("a RIP-relative load still reads the original data after relocation",
          "[hooks_trampoline]") {
    // 0:  mov rax, [rip+0x0F]   -> reads the 8 bytes at offset 0x16
    // 7:  nop x8                -> pads the patch window to 15 bytes
    // 15: ret
    // 22: 0xC0FFEE5EA51DE
    static const unsigned char code[] = {
        0x48, 0x8B, 0x05, 0x0F, 0x00, 0x00, 0x00,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0xC3,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0xDE, 0x51, 0xEA, 0x55, 0xEE, 0xFF, 0x0C, 0x00,
    };
    ExecutableBuffer buffer(code, sizeof(code));
    auto callable = reinterpret_cast<uint64_t (*)()>(buffer.addr);
    REQUIRE(callable() == 0x000CFFEE55EA51DEull);

    HookHandle handle{};
    REQUIRE(hook_install_counter_probe(buffer.addr, &handle));

    // The copied load runs from the trampoline: without a rewritten
    // displacement it would read whatever sits after the trampoline instead.
    REQUIRE(callable() == 0x000CFFEE55EA51DEull);

    uint64_t count = 0;
    REQUIRE(hook_probe_hit_count(handle, &count));
    REQUIRE(count == 1);
    REQUIRE(hook_uninstall_trampoline(handle));
}

TEST_CASE("a rel32 call outside the patch window is retargeted, not left dangling",
          "[hooks_trampoline]") {
    // 0:  call +0x10           -> offset 21, past the 14-byte patch window
    // 5:  ret
    // 6:  nop x15
    // 21: mov eax, 42; ret
    static const unsigned char code[] = {
        0xE8, 0x10, 0x00, 0x00, 0x00,
        0xC3,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0xB8, 0x2A, 0x00, 0x00, 0x00,
        0xC3,
    };
    ExecutableBuffer buffer(code, sizeof(code));
    auto callable = reinterpret_cast<IntFn>(buffer.addr);
    REQUIRE(callable() == 42);

    HookHandle handle{};
    REQUIRE(hook_install_counter_probe(buffer.addr, &handle));

    // The relocated call must still land on the callee in the original buffer.
    REQUIRE(callable() == 42);

    REQUIRE(hook_uninstall_trampoline(handle));
}

TEST_CASE("an 8-bit relative branch is refused rather than mis-relocated", "[hooks_trampoline]") {
    // 0:  jmp short +0x0D      -> offset 15, outside the patch window, but the
    //                             trampoline sits pages away and the rel8 field
    //                             cannot encode that distance.
    static const unsigned char code[] = {
        0xEB, 0x0D,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
        0xB8, 0x2A, 0x00, 0x00, 0x00,
        0xC3,
    };
    ExecutableBuffer buffer(code, sizeof(code));
    auto callable = reinterpret_cast<IntFn>(buffer.addr);
    REQUIRE(callable() == 42);

    HookHandle handle{};
    REQUIRE_FALSE(hook_install_trampoline(buffer.addr, (void*)trampoline_test_detour_b, &handle, nullptr));

    // Refusing must leave the target untouched and still callable.
    REQUIRE(callable() == 42);
}


TEST_CASE("a trampoline hook refuses a target already claimed by a breakpoint hook",
          "[hooks_trampoline]") {
    // A breakpoint hook patches a single byte, so the prologue still decodes
    // and the trampoline installer would happily patch over it -- destroying
    // the byte the breakpoint hook saved -- if it did not consult the registry.
    g_target_b_ran = 0;
    HookHandle breakpoint_handle{};
    REQUIRE(hook_install_breakpoint((void*)trampoline_test_target_b,
                                    (void*)trampoline_test_detour_b, &breakpoint_handle));

    HookHandle trampoline_handle{};
    REQUIRE_FALSE(hook_install_trampoline((void*)trampoline_test_target_b,
                                          (void*)trampoline_test_detour, &trampoline_handle, nullptr));
    REQUIRE_FALSE(hook_install_counter_probe((void*)trampoline_test_target_b,
                                             &trampoline_handle));

    // The breakpoint hook is intact and still the one that fires.
    trampoline_test_target_b();
    REQUIRE(g_detour_b_ran >= 1);

    REQUIRE(hook_uninstall_breakpoint(breakpoint_handle));
    g_target_b_ran = 0;
    trampoline_test_target_b();
    REQUIRE(g_target_b_ran == 1);
}

TEST_CASE("a rel32 that cannot absorb the trampoline delta is refused", "[hooks_trampoline]") {
    // call rel32 at the extremes of the encodable range, installed at the SAME
    // target address in sequence rather than at two independently-allocated
    // addresses. mmap_near's candidate search always tries the same side
    // first for a given target page, so reusing one address means both
    // installs see the same trampoline placement (the second install reuses
    // the exact mapping the first one just freed) -- whichever extreme sits
    // on the "wrong" side of that one placement must overflow. Two unrelated
    // ExecutableBuffer allocations do not give this guarantee: each is placed
    // independently by the OS/allocator, so nothing stops both from landing
    // their trampolines on opposite sides of their own targets, in which case
    // neither overflows and this test fails intermittently.
    auto make_fixture = [](int32_t displacement) {
        std::array<unsigned char, 14> code{};
        code[0] = 0xE8;
        std::memcpy(code.data() + 1, &displacement, sizeof(displacement));
        code[5] = 0xC3;
        for (size_t i = 6; i < code.size(); ++i) code[i] = 0x90;
        return code;
    };

    const auto forward = make_fixture(INT32_MAX - 16);
    const auto backward = make_fixture(INT32_MIN + 16);
    ExecutableBuffer buffer(forward.data(), forward.size());

    // These fixtures branch into nowhere and are never called, only relocated.
    HookHandle forward_handle{};
    const bool forward_installed =
        hook_install_trampoline(buffer.addr, (void*)trampoline_test_detour_b, &forward_handle, nullptr);
    if (forward_installed) {
        REQUIRE(hook_uninstall_trampoline(forward_handle));
    }

    std::memcpy(buffer.addr, backward.data(), backward.size());
    HookHandle backward_handle{};
    const bool backward_installed =
        hook_install_trampoline(buffer.addr, (void*)trampoline_test_detour_b, &backward_handle, nullptr);
    if (backward_installed) {
        REQUIRE(hook_uninstall_trampoline(backward_handle));
    }

    REQUIRE_FALSE((forward_installed && backward_installed));
}

TEST_CASE("a 17-byte prologue is accepted and its original stays callable", "[hooks_trampoline]") {
    hook_registry_reset_for_testing();
    g_prologue17_detour_ran = 0;
    g_prologue17_original = nullptr;

    ExecutableBuffer buffer(kPrologue17, sizeof(kPrologue17));
    IntFn target = (IntFn)buffer.addr;
    REQUIRE(target() == 42);

    HookHandle handle{};
    REQUIRE(hook_install_trampoline(buffer.addr, (void*)prologue17_detour, &handle, nullptr));

    g_prologue17_original = (IntFn)hook_manager_get_trampoline(handle);
    REQUIRE(g_prologue17_original != nullptr);

    REQUIRE(target() == 42);
    REQUIRE(g_prologue17_detour_ran == 1);

    REQUIRE(hook_uninstall_trampoline(handle));
    REQUIRE(target() == 42);
    REQUIRE(g_prologue17_detour_ran == 1);
}

TEST_CASE("the trampoline is available through the out-parameter", "[hooks_trampoline]") {
    hook_registry_reset_for_testing();
    g_target_ran = 0;
    g_detour_ran = 0;

    HookHandle handle{};
    void* trampoline = nullptr;
    REQUIRE(hook_install_trampoline((void*)trampoline_test_target,
                                    (void*)trampoline_test_detour, &handle, &trampoline));

    REQUIRE(trampoline != nullptr);
    REQUIRE(trampoline == hook_manager_get_trampoline(handle));

    g_original_via_trampoline = (void (*)(void))trampoline;
    trampoline_test_target();
    REQUIRE(g_detour_ran == 1);
    REQUIRE(g_target_ran == 1);

    REQUIRE(hook_uninstall_trampoline(handle));
    g_original_via_trampoline = nullptr;
}

TEST_CASE("a refused installation leaves the trampoline out-parameter null", "[hooks_trampoline]") {
    hook_registry_reset_for_testing();

    HookHandle handle{};
    void* trampoline = (void*)0xDEADBEEF;
    REQUIRE_FALSE(hook_install_trampoline(nullptr, (void*)trampoline_test_detour, &handle, &trampoline));
    REQUIRE(trampoline == nullptr);
}
