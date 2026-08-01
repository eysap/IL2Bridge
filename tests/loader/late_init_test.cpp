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
