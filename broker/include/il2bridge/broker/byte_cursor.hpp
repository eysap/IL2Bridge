#pragma once
#include <cstdint>
#include <cstddef>
#include <span>

namespace il2bridge {

// Bounds-checked reader with sticky error state. Callers may check valid()
// once after a sequence of reads.
class ByteCursor {
public:
    ByteCursor(std::span<const uint8_t> data, size_t pos)
        : data_(data), pos_(pos), valid_(pos <= data.size()) {}

    bool valid() const { return valid_; }
    size_t position() const { return pos_; }

    uint8_t read_u8();
    uint16_t read_u16();
    int32_t read_i32();
    uint32_t read_u32();
    uint64_t read_u64();

    // width must be 1, 2, or 4. Reads that many bytes and returns the value
    // as int32_t, translating the width-specific "null" sentinel (0xFF for
    // width 1, 0xFFFF for width 2, or a native negative int32 for width 4)
    // to -1. Any other width marks the cursor invalid and returns -1.
    int32_t read_variable_index(uint8_t width);

private:
    bool ensure(size_t n);

    std::span<const uint8_t> data_;
    size_t pos_;
    bool valid_;
};

} // namespace il2bridge
