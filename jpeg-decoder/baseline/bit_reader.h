#pragma once

#include <cstdint>
#include <istream>

using Word = uint16_t;

class BitReader {
public:
    BitReader() = delete;
    BitReader(const BitReader&) = delete;
    BitReader& operator=(const BitReader&) = delete;
    BitReader(BitReader&&) = default;
    BitReader& operator=(BitReader&&) = delete;

    explicit BitReader(std::istream& in) : in_(in) {
    }

    uint16_t ReadBits(uint8_t bits_cnt = 1);
    int16_t ReadBitsSigned(uint8_t bits_cnt = 1);

    uint8_t ReadByte() const;

    Word ReadWord();

    void Align();

private:
    static constexpr size_t kCharSz = sizeof(unsigned char) * 8;
    std::istream& in_;
    unsigned char buffer_{0};
    size_t buffer_size_{0};
};
