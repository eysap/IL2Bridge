#include "il2bridge/broker/byte_cursor.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace il2bridge;

TEST_CASE("reads primitives in little-endian order", "[byte_cursor]") {
    std::vector<uint8_t> bytes = {
        0x2A,                          // u8
        0x01, 0x00,                    // u16 = 1
        0x02, 0x00, 0x00, 0x00,        // i32 = 2
        0x03, 0x00, 0x00, 0x00,        // u32 = 3
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // u64 = 4
    };
    ByteCursor c(bytes, 0);
    REQUIRE(c.read_u8() == 0x2A);
    REQUIRE(c.read_u16() == 1);
    REQUIRE(c.read_i32() == 2);
    REQUIRE(c.read_u32() == 3u);
    REQUIRE(c.read_u64() == 4u);
    REQUIRE(c.valid());
}

TEST_CASE("out-of-bounds read marks cursor invalid without crashing", "[byte_cursor]") {
    std::vector<uint8_t> bytes = {0x01, 0x02};
    ByteCursor c(bytes, 0);
    c.read_u32(); // needs 4 bytes, only 2 available
    REQUIRE_FALSE(c.valid());
    REQUIRE(c.read_u8() == 0);
    REQUIRE(c.read_i32() == 0);
    REQUIRE_FALSE(c.valid());
}

TEST_CASE("constructing at a position past the end is immediately invalid", "[byte_cursor]") {
    std::vector<uint8_t> bytes = {0x01, 0x02};
    ByteCursor c(bytes, 10);
    REQUIRE_FALSE(c.valid());
}

TEST_CASE("read_variable_index width 1: value and null sentinel", "[byte_cursor]") {
    std::vector<uint8_t> bytes = {0x05, 0xFF};
    ByteCursor c(bytes, 0);
    REQUIRE(c.read_variable_index(1) == 5);
    REQUIRE(c.read_variable_index(1) == -1);
}

TEST_CASE("read_variable_index width 2: value and null sentinel", "[byte_cursor]") {
    std::vector<uint8_t> bytes = {0x34, 0x12, 0xFF, 0xFF};
    ByteCursor c(bytes, 0);
    REQUIRE(c.read_variable_index(2) == 0x1234);
    REQUIRE(c.read_variable_index(2) == -1);
}

TEST_CASE("read_variable_index width 4: passes through native int32", "[byte_cursor]") {
    std::vector<uint8_t> bytes = {0xFF, 0xFF, 0xFF, 0xFF}; // -1 as int32
    ByteCursor c(bytes, 0);
    REQUIRE(c.read_variable_index(4) == -1);
}

TEST_CASE("read_variable_index width 4: truncated read returns -1 and invalidates cursor", "[byte_cursor]") {
    std::vector<uint8_t> bytes = {0x01, 0x02}; // only 2 bytes, width 4 needs 4
    ByteCursor c(bytes, 0);
    REQUIRE(c.read_variable_index(4) == -1);
    REQUIRE_FALSE(c.valid());
}

TEST_CASE("read_variable_index rejects an invalid width", "[byte_cursor]") {
    std::vector<uint8_t> bytes = {0x01, 0x02, 0x03, 0x04};
    ByteCursor c(bytes, 0);
    REQUIRE(c.read_variable_index(3) == -1);
    REQUIRE_FALSE(c.valid());
}
