#pragma once

#include "bit_reader.h"
#include "include/huffman.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

constexpr int kU8Cnt = std::numeric_limits<uint8_t>::max() + 1;
constexpr int kU16Cnt = std::numeric_limits<uint16_t>::max() + 1;

struct QuantumTable {
    QuantumTable(uint8_t table_id, const std::vector<uint16_t> &data)
        : table_id(table_id), data(data) {
    }

    uint8_t table_id;
    std::vector<uint16_t> data;
};

struct Huffman {
    Huffman(bool is_dc, uint8_t table_id, HuffmanTree &&tree)
        : is_dc(is_dc), table_id(table_id), tree(std::move(tree)) {
    }

    bool is_dc;
    uint8_t table_id;
    HuffmanTree tree;
};

struct ChannelMetadata {
    ChannelMetadata() = default;
    ChannelMetadata(uint8_t id, uint8_t h, uint8_t v, uint8_t quant_id)
        : channel_id(id), h(h), v(v), quant_id(quant_id) {
    }
    uint8_t channel_id, h, v, quant_id;
};

struct ImageMetadata {
    ImageMetadata(uint8_t precision, uint8_t channels_cnt, uint16_t height, uint16_t width,
                  const std::vector<ChannelMetadata> &channels);

    uint8_t precision, channels_cnt;
    uint16_t height, width;
    std::vector<ChannelMetadata> channels;

    const ChannelMetadata &GetMetaByChannelId(uint8_t channel_id) const;
};

struct ImageData {
    ImageData(std::vector<std::vector<std::vector<int16_t>>> &&channel_matrix,
              const std::vector<uint8_t> &channel_ids, uint16_t mcu_h, uint16_t mcu_w)
        : channel_matrix(std::move(channel_matrix)),
          channel_ids(channel_ids),
          mcu_h(mcu_h),
          mcu_w(mcu_w) {
    }
    std::vector<std::vector<std::vector<int16_t>>> channel_matrix;
    std::vector<uint8_t> channel_ids;
    uint16_t mcu_h, mcu_w;
};

struct RawImage {
    RawImage(ImageData &&data, const ImageMetadata &meta, const std::string &comment,
             const std::array<std::optional<QuantumTable>, kU8Cnt> &quantum_tables)
        : comment(comment), data(std::move(data)), metadata(meta), quantum_tables(quantum_tables) {
    }

    std::string comment;
    ImageData data;
    ImageMetadata metadata;
    std::array<std::optional<QuantumTable>, kU8Cnt> quantum_tables;
};

class Parser {
public:
    explicit Parser(std::istream &is) : bit_reader_(is) {
    }

    RawImage ReadRawImage();

private:
    enum class MarkerType {
        BeginFile,
        EndFile,
        Comment,
        APPn,
        Quant,
        Meta,
        Huffman,
        Data,
    };

    constexpr static std::array<std::optional<MarkerType>, kU16Cnt> GetMarkerArr();
    MarkerType ReadMarkerType();
    Word ReadSz();
    uint8_t ReadFromHuffmanTree(HuffmanTree *tree);
    std::vector<int16_t> ReadBlock(HuffmanTree *, HuffmanTree *, int16_t &);
    std::string ReadComment();
    ImageMetadata ReadImageMeta();
    std::vector<QuantumTable> ReadQuantTable();
    std::vector<Huffman> ReadHuffmanTree();
    ImageData ReadImageData(std::array<std::optional<HuffmanTree>, kU8Cnt * 2> &,
                            const ImageMetadata &);
    BitReader bit_reader_;
    static const std::array<std::optional<MarkerType>, kU16Cnt> kWordToMarkerType;
};