#pragma once
#include "il2bridge/broker/metadata_structs.hpp"
#include <cstdint>
#include <optional>

namespace il2bridge {

// The 4 index-width categories that are actually dynamic at metadata version
// 39 (everything else — Method, Field, Event, Property, NestedType, etc. —
// only becomes dynamic at v104+/v105+/v106+, so stays fixed 4-byte int32 for
// us). See design doc 2026-07-30 section 2.
struct IndexWidths {
    uint8_t type_width;              // Il2CppType index width
    uint8_t type_definition_width;   // Il2CppTypeDefinition index width
    uint8_t generic_container_width; // Il2CppGenericContainer index width
    uint8_t parameter_width;         // Il2CppParameterDefinition index width
};

// count <= 255 -> 1 byte, count <= 65535 -> 2 bytes, else -> 4 bytes.
// Negative counts (corrupt input) are treated as full width defensively.
uint8_t get_index_width(int32_t count);

// Derives all 4 widths from the already-validated header. Returns nullopt if
// header.interfaceOffsets.count is 0 (can't derive type_width — division by
// zero) or if the derived type_width isn't 1/2/4 (corrupt/unexpected file).
std::optional<IndexWidths> compute_index_widths(const Il2CppGlobalMetadataHeader& header);

} // namespace il2bridge
