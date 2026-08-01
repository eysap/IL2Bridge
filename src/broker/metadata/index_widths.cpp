#include "il2bridge/broker/index_widths.hpp"

namespace il2bridge {

uint8_t get_index_width(int32_t count) {
    if (count < 0) return 4;
    if (count <= 255) return 1;
    if (count <= 65535) return 2;
    return 4;
}

std::optional<IndexWidths> compute_index_widths(const Il2CppGlobalMetadataHeader& header) {
    if (header.interfaceOffsets.count <= 0 || header.interfaceOffsets.size < 0) {
        return std::nullopt;
    }
    if (header.interfaceOffsets.size % header.interfaceOffsets.count != 0) {
        return std::nullopt; // not an exact multiple: corrupt/unexpected section, can't trust any derived width
    }

    // Widen to int64_t so the subtraction below can never overflow, even for
    // adversarial/corrupt header values that reach this function unvalidated
    // (interfaceOffsets.size/.count are not range-checked by the caller).
    int64_t bytes_per_interface_offset = static_cast<int64_t>(header.interfaceOffsets.size) / header.interfaceOffsets.count;
    int64_t type_width_wide = bytes_per_interface_offset - static_cast<int64_t>(sizeof(int32_t));
    if (type_width_wide != 1 && type_width_wide != 2 && type_width_wide != 4) {
        return std::nullopt;
    }
    int32_t type_width_signed = static_cast<int32_t>(type_width_wide);

    IndexWidths widths{};
    widths.type_width = static_cast<uint8_t>(type_width_signed);
    widths.type_definition_width = get_index_width(header.typeDefinitions.count);
    widths.generic_container_width = get_index_width(header.genericContainers.count);
    widths.parameter_width = get_index_width(header.parameters.count);
    return widths;
}

} // namespace il2bridge
