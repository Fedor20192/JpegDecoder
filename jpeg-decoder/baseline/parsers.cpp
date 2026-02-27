#include "parsers.h"

#include <cmath>
#include <glog/logging.h>
#include <optional>

constexpr uint8_t kLowestByteMask = 0xf, kBlockSz = 64;

const ChannelMetadata& ImageMetadata::GetMetaByChannelId(uint8_t channel_id) const {
    for (size_t i = 0; i < channels.size(); ++i) {
        if (channels[i].channel_id == channel_id) {
            return channels[i];
        }
    }
    throw std::runtime_error("No meta for channel");
}

template <typename T>
std::vector<T> GetZigZag(const std::vector<T>& data) {
    if (data.size() != kBlockSz) {
        // DLOG(ERROR) << "Bad block size for zig-zag: " << data.size() << "\n";
        throw std::runtime_error("Bad block size for zig-zag");
    }
    uint8_t dim = std::sqrt(data.size());
    std::vector<std::vector<T>> dub(dim, std::vector<T>(dim));

    size_t index = 0;
    for (int diag = 0; diag < 2 * dim - 1; ++diag) {
        if (diag % 2 == 0) {
            int row = std::min(diag, dim - 1), col = diag - row;
            while (row >= 0 && col <= dim - 1) {
                if (index < kBlockSz) {
                    dub[row][col] = data[index++];
                }
                --row, ++col;
            }
        } else {
            int col = std::min(diag, dim - 1), row = diag - col;
            while (col >= 0 && row <= dim - 1) {
                if (index < kBlockSz) {
                    dub[row][col] = data[index++];
                }
                --col, ++row;
            }
        }
    }

    std::vector<T> result;
    result.reserve(data.size());
    for (size_t i = 0; i < dub.size(); ++i) {
        for (size_t j = 0; j < dub[i].size(); ++j) {
            result.push_back(dub[i][j]);
        }
    }
    return result;
}

ImageMetadata::ImageMetadata(uint8_t precision, uint8_t channels_cnt, uint16_t height,
                             uint16_t width, const std::vector<ChannelMetadata>& channels)
    : precision(precision),
      channels_cnt(channels_cnt),
      height(height),
      width(width),
      channels(channels) {
    if (channels_cnt != channels.size()) {
        // DLOG(ERROR) << "Channels cnt: " << channels_cnt << "\nChannels size: " << channels.size();
        throw std::runtime_error("Channels size");
    }
}

const std::map<Word, Parser::MarkerType> Parser::kWordToMarkerType = {
    {0xffd8, MarkerType::BeginFile}, {0xffd9, MarkerType::EndFile}, {0xfffe, MarkerType::Comment},
    {0xffdb, MarkerType::Quant},     {0xffc0, MarkerType::Meta},    {0xffc4, MarkerType::Huffman},
    {0xffda, MarkerType::Data},      {0xffe0, MarkerType::APPn},    {0xffe1, MarkerType::APPn},
    {0xffe2, MarkerType::APPn},      {0xffe3, MarkerType::APPn},    {0xffe4, MarkerType::APPn},
    {0xffe5, MarkerType::APPn},      {0xffe6, MarkerType::APPn},    {0xffe7, MarkerType::APPn},
    {0xffe8, MarkerType::APPn},      {0xffe9, MarkerType::APPn},    {0xffea, MarkerType::APPn},
    {0xffeb, MarkerType::APPn},      {0xffec, MarkerType::APPn},    {0xffed, MarkerType::APPn},
    {0xffee, MarkerType::APPn},      {0xffef, MarkerType::APPn},
};

RawImage Parser::ReadRawImage() {
    // DLOG(INFO) << "Start reading raw image\n";

    if (ReadMarkerType() != MarkerType::BeginFile) {
        // DLOG(ERROR) << "No begin marker\n";
        throw std::runtime_error("No begin marker");
    }

    std::map<uint8_t, QuantumTable> quantum_tables;
    std::string comment;
    std::optional<ImageData> image_data{std::nullopt};
    std::optional<ImageMetadata> metadata{std::nullopt};
    std::map<std::pair<uint8_t, bool>, HuffmanTree> huffman_trees;

    MarkerType marker;
    while ((marker = ReadMarkerType()) != MarkerType::EndFile) {
        if (marker == MarkerType::Meta) {
            if (metadata.has_value()) {
                // DLOG(ERROR) << "Two SOF markers\n";
                throw std::runtime_error("Two SOF markers");
            }
            metadata = ReadImageMeta();
        } else if (marker == MarkerType::Comment) {
            comment = ReadComment();
        } else if (marker == MarkerType::Quant) {
            std::vector<QuantumTable> quantum_table = ReadQuantTable();
            for (size_t i = 0; i < quantum_table.size(); ++i) {
                uint8_t table_id = quantum_table[i].table_id;
                if (quantum_tables.contains(table_id)) {
                    // DLOG(ERROR) << "Two or more quantum tables with one id\n";
                    throw std::runtime_error("Two or more quantum tables with one id");
                }
                quantum_tables.insert(std::make_pair(table_id, std::move(quantum_table[i])));
            }
        } else if (marker == MarkerType::Huffman) {
            std::vector<Huffman> huffman_tree = ReadHuffmanTree();
            for (size_t i = 0; i < huffman_tree.size(); ++i) {
                uint8_t tree_id = huffman_tree[i].table_id;
                bool is_dc = huffman_tree[i].is_dc;
                if (huffman_trees.contains(std::make_pair(tree_id, is_dc))) {
                    // DLOG(ERROR) << "Two or more huffman trees with one id\n";
                    throw std::runtime_error("Two or more huffman trees with one id");
                }
                huffman_trees.insert(std::make_pair(std::make_pair(tree_id, is_dc),
                                                    std::move(huffman_tree[i].tree)));
            }
        } else if (marker == MarkerType::Data) {
            if (!metadata.has_value()) {
                // DLOG(ERROR) << "No metadata before reading image data\n";
                throw std::runtime_error("No metadata before reading image data");
            }
            image_data = ReadImageData(huffman_trees, metadata.value());
            bit_reader_.Align();
        } else if (marker == MarkerType::BeginFile) {
            // DLOG(ERROR) << "Begin marker in bad place\n";
            throw std::runtime_error("Begin marker in bad place");
        } else if (marker == MarkerType::APPn) {
            ReadComment();
        }
    }

    if (!image_data.has_value() || !metadata.has_value()) {
        // DLOG_IF(ERROR, !image_data.has_value()) << "No image data in file\n";
        // DLOG_IF(ERROR, !metadata.has_value()) << "No metadata in file\n";
        throw std::runtime_error("No image/meta data in file");
    }
    // DLOG(INFO) << "Finished reading raw image\n";
    return RawImage(image_data.value(), metadata.value(), comment, quantum_tables);
}

Parser::MarkerType Parser::ReadMarkerType() {
    const Word word = bit_reader_.ReadWord();
    if (kWordToMarkerType.contains(word)) {
        return kWordToMarkerType.at(word);
    }
    // DLOG(ERROR) << "Unknown marker " << std::hex << word << '\n';
    throw std::runtime_error("Unknown marker");
}

Word Parser::ReadSz() {
    const Word sz = bit_reader_.ReadWord();
    if (sz < 2) {
        // DLOG(ERROR) << "Too little comment section size: " << sz << '\n';
        throw std::runtime_error("Too little comment section size");
    }
    return sz - 2;
}

uint8_t Parser::ReadFromHuffmanTree(HuffmanTree* tree) {
    int ans;
    while (!tree->Move(bit_reader_.ReadBits(), ans)) {
    }
    return ans;
}

std::vector<int16_t> Parser::ReadBlock(HuffmanTree* dc_tree, HuffmanTree* ac_tree,
                                       int16_t& prev_dc) {
    // DLOG(INFO) << "Start reading block\n";
    std::vector<int16_t> matrix;
    matrix.reserve(kBlockSz);

    const uint8_t dc_sz = ReadFromHuffmanTree(dc_tree);
    if (dc_sz == 0) {
        matrix.push_back(prev_dc);
    } else {
        const int16_t diff_dc = bit_reader_.ReadBitsSigned(dc_sz);
        prev_dc += diff_dc;
        matrix.push_back(prev_dc);
    }

    while (matrix.size() < kBlockSz) {
        const uint8_t mask = ReadFromHuffmanTree(ac_tree);
        if (mask == 0) {
            matrix.resize(kBlockSz, 0);
            break;
        }
        const uint8_t zeros_cnt = mask >> 4;
        const uint8_t ac_sz = mask & kLowestByteMask;
        // DLOG(INFO) << "AC size: " << static_cast<int>(ac_sz)
        //            << " Zeros count: " << static_cast<int>(zeros_cnt) << '\n';
        for (uint8_t i = 0; i < zeros_cnt; ++i) {
            matrix.push_back(0);
        }

        if (ac_sz != 0 || zeros_cnt == 15) {
            matrix.push_back(bit_reader_.ReadBitsSigned(ac_sz));
            // DLOG(INFO) << "AC reed: " << matrix.back() << '\n';
        } else {
            // DLOG(ERROR) << "Empty ac coef\n";
            throw std::runtime_error("Empty ac coef");
        }
    }

    if (matrix.size() != kBlockSz) {
        // DLOG(ERROR) << "Matrix sz: " << matrix.size() << " != " << static_cast<int>(kBlockSz)
        //             << '\n';
        throw std::runtime_error("Too many blocks in matrix");
    }

    // DLOG(INFO) << "Finished reading block\n";
    return GetZigZag(matrix);
}

std::string Parser::ReadComment() {
    // DLOG(INFO) << "Start reading comment\n";
    auto sz = ReadSz();
    std::string comment;
    while (sz-- > 0) {
        comment += bit_reader_.ReadByte();
    }
    // DLOG(INFO) << "Finish reading comment\n Comment: " << '\n';
    return comment;
}

ImageMetadata Parser::ReadImageMeta() {
    // DLOG(INFO) << "Start reading image metadata\n";
    auto sz = ReadSz();

    if (sz < 6) {
        // DLOG(ERROR) << "Too little image metadata size: " << sz << '\n';
        throw std::runtime_error("Too little image metadata size");
    }
    sz -= 6;

    const uint8_t precision = bit_reader_.ReadByte();
    const uint16_t height = bit_reader_.ReadWord();
    const uint16_t width = bit_reader_.ReadWord();
    const uint8_t channels_cnt = bit_reader_.ReadByte();

    if (height == 0 || width == 0) {
        // DLOG(ERROR) << "Empty image\n";
        throw std::runtime_error("Empty image");
    }

    if (sz != channels_cnt * 3) {
        // DLOG_IF(ERROR, sz < channels_cnt * 3) << "Too little image metadata size: " << sz
        //                                       << "\nChannels cnt: " << channels_cnt << '\n';
        // DLOG_IF(ERROR, sz > channels_cnt * 3)
        //     << "Too big image metadata size: " << sz << "\nChannels cnt: " << channels_cnt << '\n';
        throw std::runtime_error("Bad metadata size");
    }
    std::vector<ChannelMetadata> channels_info;
    for (uint8_t c = 0; c < channels_cnt; ++c) {
        uint8_t id = bit_reader_.ReadByte();
        const uint8_t hv = bit_reader_.ReadByte();
        uint8_t h = hv >> 4, v = hv & kLowestByteMask;
        uint8_t quant_id = bit_reader_.ReadByte();
        channels_info.emplace_back(id, h, v, quant_id);
    }
    // DLOG(INFO) << "Finish reading image metadata\n";
    return ImageMetadata(precision, channels_cnt, height, width, channels_info);
}

std::vector<QuantumTable> Parser::ReadQuantTable() {
    // DLOG(INFO) << "Start reading quantum table\n";

    auto sz = ReadSz();

    std::vector<QuantumTable> ans;
    while (sz > 0) {
        if (sz-- < 1) {
            // DLOG(ERROR) << "Too small quantum section size: " << sz << '\n';
            throw std::runtime_error("Too small quantum section size");
        }

        const uint8_t mask = bit_reader_.ReadByte();
        const uint8_t value_len = (mask >> 4) == 1 ? 2 : 1;
        if ((mask >> 4) != 0 && (mask >> 4) != 1) {
            // DLOG(ERROR) << "Len > 1\n";
            throw std::runtime_error("Too big len");
        }
        // DLOG(INFO) << "Value len = " << static_cast<unsigned>(value_len) << '\n';
        const uint8_t quant_id = mask & kLowestByteMask;

        if (sz < kBlockSz * value_len) {
            // DLOG(INFO) << "Sz neq quantum size: " << sz << " != " << kBlockSz * value_len << '\n';
            throw std::runtime_error("Bad quantum size");
        }
        sz -= kBlockSz * value_len;

        std::vector<uint16_t> data;
        data.reserve(sz);
        for (size_t i = 0; i < kBlockSz; ++i) {
            uint16_t val;
            if (value_len == 1) {
                val = bit_reader_.ReadByte();
            } else {
                val = bit_reader_.ReadWord();
            }
            data.push_back(val);
        }

        if (data.size() != kBlockSz) {
            // DLOG(ERROR) << "Quantum table size not is 64\nReal size: " << data.size() << '\n';
            throw std::runtime_error("Quantum table size not is 64");
        }

        ans.emplace_back(quant_id, GetZigZag(data));
    }

    // DLOG(INFO) << "Finished reading quantum tables" << '\n';
    return ans;
}

std::vector<Huffman> Parser::ReadHuffmanTree() {
    // DLOG(INFO) << "Start reading Huffman tree\n";

    auto sz = ReadSz();

    std::vector<Huffman> ans;
    while (sz > 0) {
        if (sz-- < 17) {
            // DLOG(ERROR) << "Too small huffman section size: " << sz << '\n';
            throw std::runtime_error("Too small huffman section size");
        }
        const uint8_t mask = bit_reader_.ReadByte();
        const bool is_dc = (mask >> 4) == 0;
        const uint8_t table_id = mask & kLowestByteMask;

        unsigned sum_lengths = 0;
        std::vector<uint8_t> code_lengths(16);
        for (size_t i = 0; i < code_lengths.size(); ++i, --sz) {
            code_lengths[i] = bit_reader_.ReadByte();
            sum_lengths += code_lengths[i];
        }

        if (sum_lengths > sz) {
            // DLOG(ERROR) << "Sum code lengths in huffman > section sz\nSum = " << sum_lengths
            //             << "\nSz = " << sz << '\n';
            throw std::runtime_error("Bad Huffman table size");
        }

        std::vector<uint8_t> values(sum_lengths);
        for (unsigned i = 0; i < sum_lengths; ++i, --sz) {
            values[i] = bit_reader_.ReadByte();
        }

        auto tree = HuffmanTree();
        tree.Build(code_lengths, values);
        ans.emplace_back(is_dc, table_id, std::move(tree));
    }
    // DLOG(INFO) << "Finished reading Huffman tree\n";
    return ans;
}

ImageData Parser::ReadImageData(std::map<std::pair<uint8_t, bool>, HuffmanTree>& huffman_trees,
                                const ImageMetadata& meta) {
    // DLOG(INFO) << "Start reading image data\n";
    auto sz = ReadSz();

    if (sz-- < 1) {
        // DLOG(ERROR) << "No info about channels cnt\n";
        throw std::runtime_error("No info about channels cnt");
    }

    const uint8_t channels_cnt = bit_reader_.ReadByte();

    if (sz < channels_cnt * 2) {
        // DLOG(ERROR) << "Image data size is not valid: " << sz << " != " << channels_cnt * 2 << '\n';
        throw std::runtime_error("Bad image data size");
    }
    sz -= channels_cnt * 2;

    std::vector<uint8_t> channel_ids(channels_cnt);
    std::vector<HuffmanTree*> dc_trees(channels_cnt);
    std::vector<HuffmanTree*> ac_trees(channels_cnt);

    for (uint8_t c = 0; c < channels_cnt; ++c) {
        channel_ids[c] = bit_reader_.ReadByte();
        const uint8_t mask = bit_reader_.ReadByte();
        uint8_t dc_id = mask >> 4, ac_id = mask & kLowestByteMask;

        if (!huffman_trees.contains(std::make_pair(dc_id, true))) {
            // DLOG(ERROR) << "No dc huffman tree found for channel: " << static_cast<int>(c) << '\n';
            throw std::runtime_error("No huffman table found");
        }

        if (!huffman_trees.contains(std::make_pair(ac_id, false))) {
            // DLOG(ERROR) << "No ac huffman tree found for channel: " << static_cast<int>(c) << "\n";
            throw std::runtime_error("No huffman table found");
        }

        dc_trees[c] = &huffman_trees.at(std::make_pair(dc_id, true));
        ac_trees[c] = &huffman_trees.at(std::make_pair(ac_id, false));
    }

    if (sz < 3) {
        // DLOG(ERROR) << "Not enough bytes for SOS spectral selection\n";
        throw std::runtime_error("Bad SOS header");
    }

    (void)bit_reader_.ReadByte();
    if (const auto se = bit_reader_.ReadByte(); se != 63) {
        // DLOG(ERROR) << se << '\n';
        throw std::runtime_error("Inconsistent SOS for baseline JPEG");
    }
    (void)bit_reader_.ReadByte();
    sz -= 3;

    while (sz-- > 0) {
        (void)bit_reader_.ReadByte();
    }

    uint8_t h_max = 0, v_max = 0;
    for (size_t i = 0; i < meta.channels.size(); ++i) {
        h_max = std::max(h_max, meta.channels[i].h);
        v_max = std::max(v_max, meta.channels[i].v);
    }
    const uint16_t mcu_h = (meta.height + 8 * v_max - 1) / (8 * v_max);
    const uint16_t mcu_w = (meta.width + 8 * h_max - 1) / (8 * h_max);

    // DLOG(INFO) << "Ended reading meta in image data\nChannels cnt: "
    //            << static_cast<int>(channels_cnt) << "\nMCU_H: " << mcu_h << "\nMCU_W: " << mcu_w
    //            << '\n';

    std::vector<int16_t> prev_dc(channels_cnt, 0);
    std::vector<std::vector<std::vector<int16_t>>> channel_matrix(channels_cnt);
    for (uint16_t mcu_y = 0; mcu_y < mcu_h; ++mcu_y) {
        for (uint16_t mcu_x = 0; mcu_x < mcu_w; ++mcu_x) {
            for (uint8_t c = 0; c < channels_cnt; ++c) {
                const uint8_t channel_id = channel_ids[c];
                const auto channel_meta = meta.GetMetaByChannelId(channel_id);
                const uint8_t h = channel_meta.h, v = channel_meta.v;

                for (uint8_t block_v = 0; block_v < v; ++block_v) {
                    for (uint8_t block_h = 0; block_h < h; ++block_h) {
                        auto block = ReadBlock(dc_trees[c], ac_trees[c], prev_dc[c]);
                        channel_matrix[c].push_back(block);
                    }
                }
            }
        }
    }

    // DLOG(INFO) << "Finished reading image data\n";
    return ImageData(channel_matrix, channel_ids, mcu_h, mcu_w);
}
