#include "il2bridge/broker/byte_cursor.hpp"
#include <cstring>

namespace il2bridge {

bool ByteCursor::ensure(size_t n) {
    if (!valid_ || pos_ + n > data_.size()) {
        valid_ = false;
        return false;
    }
    return true;
}

uint8_t ByteCursor::read_u8() {
    if (!ensure(1)) return 0;
    uint8_t v = data_[pos_];
    pos_ += 1;
    return v;
}

uint16_t ByteCursor::read_u16() {
    if (!ensure(2)) return 0;
    uint16_t v;
    std::memcpy(&v, data_.data() + pos_, 2);
    pos_ += 2;
    return v;
}

int32_t ByteCursor::read_i32() {
    if (!ensure(4)) return 0;
    int32_t v;
    std::memcpy(&v, data_.data() + pos_, 4);
    pos_ += 4;
    return v;
}

uint32_t ByteCursor::read_u32() {
    if (!ensure(4)) return 0;
    uint32_t v;
    std::memcpy(&v, data_.data() + pos_, 4);
    pos_ += 4;
    return v;
}

uint64_t ByteCursor::read_u64() {
    if (!ensure(8)) return 0;
    uint64_t v;
    std::memcpy(&v, data_.data() + pos_, 8);
    pos_ += 8;
    return v;
}

int32_t ByteCursor::read_variable_index(uint8_t width) {
    switch (width) {
        case 1: {
            uint8_t v = read_u8();
            if (!valid_) return -1;
            return v == 0xFF ? -1 : static_cast<int32_t>(v);
        }
        case 2: {
            uint16_t v = read_u16();
            if (!valid_) return -1;
            return v == 0xFFFF ? -1 : static_cast<int32_t>(v);
        }
        case 4: {
            int32_t v = read_i32();
            return valid_ ? v : -1;
        }
        default:
            valid_ = false;
            return -1;
    }
}

} // namespace il2bridge
