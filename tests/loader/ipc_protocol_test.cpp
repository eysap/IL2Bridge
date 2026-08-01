#include "il2bridge/loader/ipc.h"
#include <catch2/catch_test_macros.hpp>
#include <cstring>

TEST_CASE("parses a bare command with no arguments", "[ipc_protocol]") {
    char line[] = "PING";
    const char* command = nullptr;
    const char* args[4] = {};
    int argc = ipc_parse_line(line, &command, args, 4);

    REQUIRE(argc == 0);
    REQUIRE(std::strcmp(command, "PING") == 0);
}

TEST_CASE("parses a command with several arguments", "[ipc_protocol]") {
    char line[] = "RESOLVE-METHOD 3 SomeMethod 2";
    const char* command = nullptr;
    const char* args[4] = {};
    int argc = ipc_parse_line(line, &command, args, 4);

    REQUIRE(argc == 3);
    REQUIRE(std::strcmp(command, "RESOLVE-METHOD") == 0);
    REQUIRE(std::strcmp(args[0], "3") == 0);
    REQUIRE(std::strcmp(args[1], "SomeMethod") == 0);
    REQUIRE(std::strcmp(args[2], "2") == 0);
}

TEST_CASE("rejects an empty line", "[ipc_protocol]") {
    char line[] = "";
    const char* command = nullptr;
    const char* args[4] = {};
    REQUIRE(ipc_parse_line(line, &command, args, 4) == -1);
}

TEST_CASE("rejects a whitespace-only line", "[ipc_protocol]") {
    char line[] = "   ";
    const char* command = nullptr;
    const char* args[4] = {};
    REQUIRE(ipc_parse_line(line, &command, args, 4) == -1);
}

TEST_CASE("truncates extra arguments beyond max_args rather than overflowing", "[ipc_protocol]") {
    char line[] = "CALL a b c d e";
    const char* command = nullptr;
    const char* args[2] = {};
    int argc = ipc_parse_line(line, &command, args, 2);

    REQUIRE(argc == 2);
    REQUIRE(std::strcmp(args[0], "a") == 0);
    REQUIRE(std::strcmp(args[1], "b") == 0);
}

TEST_CASE("collapses repeated spaces between tokens", "[ipc_protocol]") {
    char line[] = "HOOK   5     log-1string-arg";
    const char* command = nullptr;
    const char* args[4] = {};
    int argc = ipc_parse_line(line, &command, args, 4);

    REQUIRE(argc == 2);
    REQUIRE(std::strcmp(args[0], "5") == 0);
    REQUIRE(std::strcmp(args[1], "log-1string-arg") == 0);
}

TEST_CASE("max_args of zero parses the command and reports zero arguments without touching args_out", "[ipc_protocol]") {
    char line[] = "RESOLVE-METHOD 3 SomeMethod";
    const char* command = nullptr;
    // Sentinel value so we can tell if ipc_parse_line ever writes to this slot.
    const char* sentinel = "untouched";
    const char* args[1] = { sentinel };
    int argc = ipc_parse_line(line, &command, args, 0);

    REQUIRE(argc == 0);
    REQUIRE(std::strcmp(command, "RESOLVE-METHOD") == 0);
    REQUIRE(args[0] == sentinel);
}

TEST_CASE("trailing whitespace after the last argument does not produce a phantom empty argument", "[ipc_protocol]") {
    char line[] = "HOOK 5   ";
    const char* command = nullptr;
    const char* args[4] = {};
    int argc = ipc_parse_line(line, &command, args, 4);

    REQUIRE(argc == 1);
    REQUIRE(std::strcmp(command, "HOOK") == 0);
    REQUIRE(std::strcmp(args[0], "5") == 0);
}

TEST_CASE("a bare command with only trailing whitespace still parses with zero arguments", "[ipc_protocol]") {
    char line[] = "PING   ";
    const char* command = nullptr;
    const char* args[4] = {};
    int argc = ipc_parse_line(line, &command, args, 4);

    REQUIRE(argc == 0);
    REQUIRE(std::strcmp(command, "PING") == 0);
}

TEST_CASE("a single-character command parses correctly", "[ipc_protocol]") {
    char line[] = "X";
    const char* command = nullptr;
    const char* args[4] = {};
    int argc = ipc_parse_line(line, &command, args, 4);

    REQUIRE(argc == 0);
    REQUIRE(std::strcmp(command, "X") == 0);
}

TEST_CASE("leading whitespace before the command is tolerated and skipped", "[ipc_protocol]") {
    char line[] = "   PING";
    const char* command = nullptr;
    const char* args[4] = {};
    int argc = ipc_parse_line(line, &command, args, 4);

    REQUIRE(argc == 0);
    REQUIRE(std::strcmp(command, "PING") == 0);
}

TEST_CASE("leading whitespace before a command with arguments is tolerated", "[ipc_protocol]") {
    char line[] = "  HOOK 5 log-1string-arg";
    const char* command = nullptr;
    const char* args[4] = {};
    int argc = ipc_parse_line(line, &command, args, 4);

    REQUIRE(argc == 2);
    REQUIRE(std::strcmp(command, "HOOK") == 0);
    REQUIRE(std::strcmp(args[0], "5") == 0);
    REQUIRE(std::strcmp(args[1], "log-1string-arg") == 0);
}

TEST_CASE("truncation still scans the entire line when tokens vastly exceed max_args", "[ipc_protocol]") {
    char line[] = "CALL t0 t1 t2 t3 t4 t5 t6 t7 t8 t9 t10 t11 t12 t13 t14 t15 t16 t17 t18 t19";
    const char* command = nullptr;
    const char* args[3] = {};
    int argc = ipc_parse_line(line, &command, args, 3);

    REQUIRE(argc == 3);
    REQUIRE(std::strcmp(command, "CALL") == 0);
    REQUIRE(std::strcmp(args[0], "t0") == 0);
    REQUIRE(std::strcmp(args[1], "t1") == 0);
    REQUIRE(std::strcmp(args[2], "t2") == 0);
}
