#pragma once
// Small shared byte-buffer builder helpers used by metadata_decode_test.cpp
// and metadata_file_test.cpp to hand-construct little-endian record bytes
// for fixtures. Kept `inline` since this header is included by multiple
// test translation units.
#include <cstdint>
#include <cstring>
#include <vector>

namespace il2bridge::test {

inline void push_i32(std::vector<uint8_t>& b, int32_t v) {
    uint8_t bytes[4];
    std::memcpy(bytes, &v, 4);
    b.insert(b.end(), bytes, bytes + 4);
}
inline void push_u32(std::vector<uint8_t>& b, uint32_t v) {
    uint8_t bytes[4];
    std::memcpy(bytes, &v, 4);
    b.insert(b.end(), bytes, bytes + 4);
}
inline void push_u16(std::vector<uint8_t>& b, uint16_t v) {
    uint8_t bytes[2];
    std::memcpy(bytes, &v, 2);
    b.insert(b.end(), bytes, bytes + 2);
}
inline void push_u64(std::vector<uint8_t>& b, uint64_t v) {
    uint8_t bytes[8];
    std::memcpy(bytes, &v, 8);
    b.insert(b.end(), bytes, bytes + 8);
}

// Writes a variable-width index value. width must be 1, 2, or 4.
inline void push_variable_index(std::vector<uint8_t>& b, uint8_t width, int32_t value) {
    if (width == 1) {
        b.push_back(value < 0 ? 0xFF : static_cast<uint8_t>(value));
    } else if (width == 2) {
        uint16_t v16 = value < 0 ? 0xFFFF : static_cast<uint16_t>(value);
        push_u16(b, v16);
    } else {
        push_i32(b, value);
    }
}

} // namespace il2bridge::test
