#include "il2bridge/loader/log.h"
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {
std::vector<std::string> g_lines;

void capture(const char* line, void* user) {
    REQUIRE(user == (void*)0x1234);
    g_lines.emplace_back(line);
}
} // namespace

TEST_CASE("a configured sink receives formatted, prefixed lines", "[log]") {
    g_lines.clear();
    il2bridge_set_log_sink(capture, (void*)0x1234);

    il2bridge_log("value is %d and name is %s", 42, "probe");

    REQUIRE(g_lines.size() == 1);
    REQUIRE(g_lines[0] == "[il2bridge] value is 42 and name is probe");

    il2bridge_set_log_sink(nullptr, nullptr);
}

TEST_CASE("clearing the sink restores the default and does not crash", "[log]") {
    g_lines.clear();
    il2bridge_set_log_sink(capture, (void*)0x1234);
    il2bridge_set_log_sink(nullptr, nullptr);

    il2bridge_log("this goes to stderr");

    REQUIRE(g_lines.empty());
}

TEST_CASE("an over-long line is truncated rather than overflowing", "[log]") {
    g_lines.clear();
    il2bridge_set_log_sink(capture, (void*)0x1234);

    std::string huge(4096, 'x');
    il2bridge_log("%s", huge.c_str());

    REQUIRE(g_lines.size() == 1);
    REQUIRE(g_lines[0].size() < 1024);
    il2bridge_set_log_sink(nullptr, nullptr);
}
