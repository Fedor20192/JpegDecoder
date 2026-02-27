#pragma once

#include "bit_reader.h"
#include "include/huffman.h"

#include <cstdint>
#include <vector>
#include <map>

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
    ImageData(const std::vector<std::vector<std::vector<int16_t>>> &channel_matrix,
              const std::vector<uint8_t> &channel_ids, uint16_t mcu_h, uint16_t mcu_w)
        : channel_matrix(channel_matrix), channel_ids(channel_ids), mcu_h(mcu_h), mcu_w(mcu_w) {
    }
    std::vector<std::vector<std::vector<int16_t>>> channel_matrix;
    std::vector<uint8_t> channel_ids;
    uint16_t mcu_h, mcu_w;
};

struct RawImage {
    RawImage(const ImageData &data, const ImageMetadata &meta, const std::string &comment,
             const std::map<uint8_t, QuantumTable> &quantum_tables)
        : comment(comment), data(data), metadata(meta), quantum_tables(quantum_tables) {
    }

    std::string comment;
    ImageData data;
    ImageMetadata metadata;
    std::map<uint8_t, QuantumTable> quantum_tables;
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

    MarkerType ReadMarkerType();
    Word ReadSz();
    uint8_t ReadFromHuffmanTree(HuffmanTree *tree);
    std::vector<int16_t> ReadBlock(HuffmanTree *, HuffmanTree *, int16_t &);
    std::string ReadComment();
    ImageMetadata ReadImageMeta();
    std::vector<QuantumTable> ReadQuantTable();
    std::vector<Huffman> ReadHuffmanTree();
    ImageData ReadImageData(std::map<std::pair<uint8_t, bool>, HuffmanTree> &,
                            const ImageMetadata &);
    BitReader bit_reader_;
    static const std::map<Word, MarkerType> kWordToMarkerType;
};