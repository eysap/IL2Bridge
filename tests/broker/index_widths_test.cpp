#include "il2bridge/broker/index_widths.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace il2bridge;

TEST_CASE("get_index_width thresholds", "[index_widths]") {
    REQUIRE(get_index_width(0) == 1);
    REQUIRE(get_index_width(255) == 1);
    REQUIRE(get_index_width(256) == 2);
    REQUIRE(get_index_width(65535) == 2);
    REQUIRE(get_index_width(65536) == 4);
    REQUIRE(get_index_width(400000) == 4);
}

TEST_CASE("get_index_width treats negative counts as full width", "[index_widths]") {
    REQUIRE(get_index_width(-1) == 4);
}

namespace {
Il2CppGlobalMetadataHeader make_header(int32_t typeDefCount, int32_t genericContainerCount,
                                        int32_t paramCount, int32_t interfaceOffsetsSize,
                                        int32_t interfaceOffsetsCount) {
    Il2CppGlobalMetadataHeader h{};
    h.typeDefinitions.count = typeDefCount;
    h.genericContainers.count = genericContainerCount;
    h.parameters.count = paramCount;
    h.interfaceOffsets.size = interfaceOffsetsSize;
    h.interfaceOffsets.count = interfaceOffsetsCount;
    return h;
}
}

TEST_CASE("compute_index_widths matches representative metadata v39 values", "[index_widths]") {
    // Verified against a production global-metadata.dat v39 header (design
    // doc 2026-07-30 section 3): typeDefinitions.count=40795,
    // genericContainers.count=6756, parameters.count=327718,
    // interfaceOffsets.size=381560, interfaceOffsets.count=47695.
    auto header = make_header(40795, 6756, 327718, 381560, 47695);
    auto widths = compute_index_widths(header);
    REQUIRE(widths.has_value());
    REQUIRE(widths->type_definition_width == 2);
    REQUIRE(widths->generic_container_width == 2);
    REQUIRE(widths->parameter_width == 4);
    REQUIRE(widths->type_width == 4); // 381560/47695 - 4 = 4
}

TEST_CASE("compute_index_widths rejects zero interfaceOffsets count", "[index_widths]") {
    auto header = make_header(100, 100, 100, 0, 0);
    REQUIRE_FALSE(compute_index_widths(header).has_value());
}

TEST_CASE("compute_index_widths rejects a non-exact size/count division", "[index_widths]") {
    // 41/8 has a remainder -- not a clean per-record byte size, must be rejected
    // rather than silently truncating and possibly landing on a "valid" width.
    auto header = make_header(100, 100, 100, 41, 8);
    REQUIRE_FALSE(compute_index_widths(header).has_value());
}

TEST_CASE("compute_index_widths rejects a negative interfaceOffsets size without overflowing", "[index_widths]") {
    auto header = make_header(100, 100, 100, -8, 2);
    REQUIRE_FALSE(compute_index_widths(header).has_value());
}

TEST_CASE("compute_index_widths resolves a 1-byte type_width", "[index_widths]") {
    // bytesPerInterfaceOffset = size/count = 40/8 = 5; type_width = 5-4 = 1.
    auto header = make_header(200, 100, 50, 40, 8);
    auto widths = compute_index_widths(header);
    REQUIRE(widths.has_value());
    REQUIRE(widths->type_width == 1);
    REQUIRE(widths->type_definition_width == 1); // 200 <= 255
    REQUIRE(widths->generic_container_width == 1); // 100 <= 255
    REQUIRE(widths->parameter_width == 1); // 50 <= 255
}
