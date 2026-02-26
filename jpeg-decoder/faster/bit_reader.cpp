#include "bit_reader.h"

#include <glog/logging.h>

uint16_t BitReader::ReadBits(uint8_t bits_cnt) {
    DLOG_IF(ERROR, bits_cnt > kCharSz * 2)
        << "Trying to read " << static_cast<int>(bits_cnt) << " > 32 bites\n";
    if (bits_cnt > kCharSz * 2) {
        throw std::runtime_error("Trying to read many bytes");
    }

    uint16_t result = 0;
    while (bits_cnt-- > 0) {
        if (buffer_size_ == 0) {
            char tmp;
            if (!in_.read(&tmp, sizeof(buffer_))) {
                DLOG(ERROR) << "Failed to read\n";
                throw std::runtime_error("EOF");
            }
            buffer_ = static_cast<unsigned char>(tmp);
            buffer_size_ = kCharSz;

            if (buffer_ == 0xff) {
                char next_char = 0;
                in_.read(&next_char, 1);
                if (!in_ || next_char != 0x00) {
                    throw std::runtime_error("Encountered marker instead of 0xFF");
                }
            }

            // DLOG(INFO) << "Read one byte: 0x" << std::hex << (static_cast<uint16_t>(buffer_) &
            // 255) << '\n';
        }
        result <<= 1u;
        result |= (buffer_ >> (kCharSz - 1)) & 1;
        --buffer_size_;
        buffer_ <<= 1u;
    }
    return result;
}

int16_t BitReader::ReadBitsSigned(uint8_t bits_cnt) {
    if (bits_cnt == 0) {
        return 0;
    }

    const uint32_t v = ReadBits(bits_cnt);

    if (v & (1u << (bits_cnt - 1))) {
        return static_cast<int16_t>(v);
    }
    return static_cast<int16_t>(static_cast<int32_t>(v) -
                                static_cast<int32_t>((1u << bits_cnt) - 1u));
}

uint8_t BitReader::ReadByte() const {
    if (buffer_size_ != 0) {
        throw std::runtime_error("Bits not aligned");
    }

    char data;
    in_.read(&data, sizeof(data));

    return static_cast<uint8_t>(data);
}

Word BitReader::ReadWord() {
    Word result = 0;
    result |= ReadByte();
    result <<= kCharSz;
    result |= ReadByte();
    return result;
}

void BitReader::Align() {
    buffer_size_ = 0;
    buffer_ = 0;
}
