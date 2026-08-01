#include <catch2/catch_test_macros.hpp>

TEST_CASE("test harness is wired up", "[sanity]") {
    REQUIRE(1 + 1 == 2);
}
