#include "il2bridge/loader/late_init.h"
#include <catch2/catch_test_macros.hpp>
#include <dlfcn.h>

namespace {

struct GameAssemblyGuard {
    void* handle = dlopen(GAMEASSEMBLY_DUT_PATH, RTLD_NOW);

    ~GameAssemblyGuard() {
        late_init_reset_for_testing();
        if (handle) {
            dlclose(handle);
        }
    }
};

int g_ready_calls_a = 0;
int g_ready_calls_b = 0;
void* g_ready_handle_a = nullptr;
void* g_ready_user_a = nullptr;

void on_ready_a(void* gameassembly_handle, void* user) {
    ++g_ready_calls_a;
    g_ready_handle_a = gameassembly_handle;
    g_ready_user_a = user;
}

void on_ready_b(void* gameassembly_handle, void* user) {
    (void)gameassembly_handle;
    (void)user;
    ++g_ready_calls_b;
}

} // namespace

TEST_CASE("watcher iteration ignores processes without GameAssembly.so", "[late_init]") {
    late_init_reset_for_testing();
    REQUIRE_FALSE(late_init_check_now_for_testing());
}

TEST_CASE("watcher finds an already-mapped GameAssembly.so", "[late_init]") {
    late_init_reset_for_testing();
    GameAssemblyGuard gameassembly;
    REQUIRE(gameassembly.handle != nullptr);
    REQUIRE(late_init_check_now_for_testing());
}

TEST_CASE("watcher iteration is idempotent once GameAssembly.so is found", "[late_init]") {
    late_init_reset_for_testing();
    GameAssemblyGuard gameassembly;
    REQUIRE(gameassembly.handle != nullptr);
    REQUIRE(late_init_check_now_for_testing());
    REQUIRE(late_init_check_now_for_testing());
}

TEST_CASE("every subscriber is notified once when readiness fires", "[late_init]") {
    late_init_reset_for_testing();
    g_ready_calls_a = 0;
    g_ready_calls_b = 0;
    int user_token = 7;

    REQUIRE(late_init_add_ready_callback(on_ready_a, &user_token));
    REQUIRE(late_init_add_ready_callback(on_ready_b, nullptr));

    int fake_module = 0;
    late_init_fire_ready_for_testing(&fake_module);

    REQUIRE(g_ready_calls_a == 1);
    REQUIRE(g_ready_calls_b == 1);
    REQUIRE(g_ready_handle_a == &fake_module);
    REQUIRE(g_ready_user_a == &user_token);

    late_init_fire_ready_for_testing(&fake_module);
    REQUIRE(g_ready_calls_a == 1);
    REQUIRE(g_ready_calls_b == 1);
}

TEST_CASE("a subscriber registering after readiness is called immediately", "[late_init]") {
    late_init_reset_for_testing();
    g_ready_calls_a = 0;

    int fake_module = 0;
    late_init_fire_ready_for_testing(&fake_module);

    REQUIRE(late_init_add_ready_callback(on_ready_a, nullptr));
    REQUIRE(g_ready_calls_a == 1);
    REQUIRE(g_ready_handle_a == &fake_module);
}

TEST_CASE("subscriber registration is bounded and reports saturation", "[late_init]") {
    late_init_reset_for_testing();
    for (int i = 0; i < IL2BRIDGE_READY_SUBSCRIBER_CAPACITY; ++i) {
        REQUIRE(late_init_add_ready_callback(on_ready_b, nullptr));
    }
    REQUIRE_FALSE(late_init_add_ready_callback(on_ready_b, nullptr));
    REQUIRE_FALSE(late_init_add_ready_callback(nullptr, nullptr));
}
