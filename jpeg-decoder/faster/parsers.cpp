#include "parsers.h"

#include <glog/logging.h>

constexpr uint8_t kLowestByteMask = 0xf, kBlockSz = 64;

uint16_t GetPairHash(uint8_t a, bool b) {
    return (static_cast<uint16_t>(a) << 1) | b;
}

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

    constexpr uint8_t kZigZagMap[kBlockSz] = {
        0,  1,  5,  6,  14, 15, 27, 28, 2,  4,  7,  13, 16, 26, 29, 42, 3,  8,  12, 17, 25, 30,
        41, 43, 9,  11, 18, 24, 31, 40, 44, 53, 10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38,
        46, 51, 55, 60, 21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63};

    std::vector<T> ans(kBlockSz);
    for (size_t i = 0; i < data.size(); ++i) {
        ans[i] = data[kZigZagMap[i]];
    }
    return ans;
}

ImageMetadata::ImageMetadata(uint8_t precision, uint8_t channels_cnt, uint16_t height,
                             uint16_t width, const std::vector<ChannelMetadata>& channels)
    : precision(precision),
      channels_cnt(channels_cnt),
      height(height),
      width(width),
      channels(channels) {
    if (channels_cnt != channels.size()) {
        // DLOG(ERROR) << "Channels cnt: " << channels_cnt << "\nChannels size: " <<
        // channels.size();
        throw std::runtime_error("Channels size");
    }
}

constexpr std::array<std::optional<Parser::MarkerType>, kU16Cnt> Parser::GetMarkerArr() {
    std::array<std::optional<MarkerType>, kU16Cnt> ans;
    ans[0xffd8] = MarkerType::BeginFile;
    ans[0xffd9] = MarkerType::EndFile;
    ans[0xfffe] = MarkerType::Comment;
    ans[0xffdb] = MarkerType::Quant;
    ans[0xffc0] = MarkerType::Meta;
    ans[0xffc4] = MarkerType::Huffman;
    ans[0xffda] = MarkerType::Data;
    ans[0xffe0] = MarkerType::APPn;
    ans[0xffe1] = MarkerType::APPn;
    ans[0xffe2] = MarkerType::APPn;
    ans[0xffe3] = MarkerType::APPn;
    ans[0xffe4] = MarkerType::APPn;
    ans[0xffe5] = MarkerType::APPn;
    ans[0xffe6] = MarkerType::APPn;
    ans[0xffe7] = MarkerType::APPn;
    ans[0xffe8] = MarkerType::APPn;
    ans[0xffe9] = MarkerType::APPn;
    ans[0xffea] = MarkerType::APPn;
    ans[0xffeb] = MarkerType::APPn;
    ans[0xffec] = MarkerType::APPn;
    ans[0xffed] = MarkerType::APPn;
    ans[0xffee] = MarkerType::APPn;
    ans[0xffef] = MarkerType::APPn;
    return ans;
}

const std::array<std::optional<Parser::MarkerType>, kU16Cnt> Parser::kWordToMarkerType =
    GetMarkerArr();

RawImage Parser::ReadRawImage() {
    // DLOG(INFO) << "Start reading raw image\n";

    if (ReadMarkerType() != MarkerType::BeginFile) {
        // DLOG(ERROR) << "No begin marker\n";
        throw std::runtime_error("No begin marker");
    }

    std::array<std::optional<QuantumTable>, kU8Cnt> quantum_tables;
    std::string comment;
    std::optional<ImageData> image_data;
    std::optional<ImageMetadata> metadata;
    std::array<std::optional<HuffmanTree>, kU8Cnt * 2> huffman_trees;

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
                if (quantum_tables[table_id].has_value()) {
                    // DLOG(ERROR) << "Two or more quantum tables with one id\n";
                    throw std::runtime_error("Two or more quantum tables with one id");
                }
                quantum_tables[table_id] = std::move(quantum_table[i]);
            }
        } else if (marker == MarkerType::Huffman) {
            std::vector<Huffman> huffman_tree = ReadHuffmanTree();
            for (size_t i = 0; i < huffman_tree.size(); ++i) {
                uint8_t tree_id = huffman_tree[i].table_id;
                bool is_dc = huffman_tree[i].is_dc;
                const uint16_t hash = GetPairHash(tree_id, is_dc);
                if (huffman_trees[hash].has_value()) {
                    // DLOG(ERROR) << "Two or more huffman trees with one id\n";
                    throw std::runtime_error("Two or more huffman trees with one id");
                }
                huffman_trees[hash] = std::move(huffman_tree[i].tree);
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
    return RawImage(std::move(image_data.value()), metadata.value(), comment, quantum_tables);
}

Parser::MarkerType Parser::ReadMarkerType() {
    const Word word = bit_reader_.ReadWord();
    if (kWordToMarkerType[word].has_value()) {
        return kWordToMarkerType[word].value();
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
    std::vector<int16_t> matrix;
    matrix.reserve(kBlockSz);

    if (const uint8_t dc_sz = ReadFromHuffmanTree(dc_tree); dc_sz == 0) {
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
        for (uint8_t i = 0; i < zeros_cnt; ++i) {
            matrix.push_back(0);
        }

        if (ac_sz != 0 || zeros_cnt == 15) {
            matrix.push_back(bit_reader_.ReadBitsSigned(ac_sz));
        } else {
            // DLOG(ERROR) << "Empty ac coef\n";
            throw std::runtime_error("Empty ac coef");
        }
    }

    if (matrix.size() != kBlockSz) {
        // DLOG(ERROR) << "Matrix sz: " << matrix.size() << " != " << static_cast<int>(kBlockSz)
        // << '\n';
        throw std::runtime_error("Too many blocks in matrix");
    }
    return GetZigZag(matrix);
}

std::string Parser::ReadComment() {
    // DLOG(INFO) << "Start reading comment\n";
    auto sz = ReadSz();
    std::string comment;
    comment.reserve(sz);
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
        //     << "Too big image metadata size: " << sz << "\nChannels cnt: " << channels_cnt <<
        //     '\n';
        throw std::runtime_error("Bad metadata size");
    }
    std::vector<ChannelMetadata> channels_info;
    channels_info.reserve(channels_cnt);
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
    ans.reserve(sz / kBlockSz + 1);
    std::vector<uint16_t> data(kBlockSz);
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
            // DLOG(INFO) << "Sz neq quantum size: " << sz << " != " << kBlockSz * value_len <<
            // '\n';
            throw std::runtime_error("Bad quantum size");
        }
        sz -= kBlockSz * value_len;
        for (size_t i = 0; i < kBlockSz; ++i) {
            uint16_t val;
            if (value_len == 1) {
                val = bit_reader_.ReadByte();
            } else {
                val = bit_reader_.ReadWord();
            }
            data[i] = val;
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
            // << "\nSz = " << sz << '\n';
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

ImageData Parser::ReadImageData(std::array<std::optional<HuffmanTree>, kU8Cnt * 2>& huffman_trees,
                                const ImageMetadata& meta) {
    // DLOG(INFO) << "Start reading image data\n";
    auto sz = ReadSz();

    if (sz-- < 1) {
        // DLOG(ERROR) << "No info about channels cnt\n";
        throw std::runtime_error("No info about channels cnt");
    }

    const uint8_t channels_cnt = bit_reader_.ReadByte();

    if (sz < channels_cnt * 2) {
        // DLOG(ERROR) << "Image data size is not valid: " << sz << " != " << channels_cnt * 2 <<
        // '\n';
        throw std::runtime_error("Bad image data size");
    }
    sz -= channels_cnt * 2;

    std::vector<uint8_t> channel_ids(channels_cnt);
    std::vector<HuffmanTree*> dc_trees(channels_cnt);
    std::vector<HuffmanTree*> ac_trees(channels_cnt);

    for (uint8_t c = 0; c < channels_cnt; ++c) {
        channel_ids[c] = bit_reader_.ReadByte();
        const uint8_t mask = bit_reader_.ReadByte();
        const uint8_t dc_id = mask >> 4, ac_id = mask & kLowestByteMask;

        const uint16_t hash_dc = GetPairHash(dc_id, true);
        const uint16_t hash_ac = GetPairHash(ac_id, false);
        if (!huffman_trees[hash_dc].has_value()) {
            // DLOG(ERROR) << "No dc huffman tree found for channel: " << static_cast<int>(c) <<
            // '\n';
            throw std::runtime_error("No huffman table found");
        }

        if (!huffman_trees[hash_ac].has_value()) {
            // DLOG(ERROR) << "No ac huffman tree found for channel: " << static_cast<int>(c) <<
            // "\n";
            throw std::runtime_error("No huffman table found");
        }

        dc_trees[c] = &huffman_trees[hash_dc].value();
        ac_trees[c] = &huffman_trees[hash_ac].value();
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

    if (h_max == 0 || v_max == 0) {
        // DLOG(ERROR) << "WHY SAMPLING FACTOR IS ZERO?!";
        throw std::runtime_error("sampling factor is zero");
    }

    const uint16_t mcu_h = (meta.height + 8 * v_max - 1) / (8 * v_max);
    const uint16_t mcu_w = (meta.width + 8 * h_max - 1) / (8 * h_max);

    // DLOG(INFO) << "Ended reading meta in image data\nChannels cnt: "
    //            << static_cast<int>(channels_cnt) << "\nMCU_H: " << mcu_h << "\nMCU_W: " << mcu_w
    //            << '\n';

    std::vector<int16_t> prev_dc(channels_cnt, 0);
    std::vector<std::vector<std::vector<int16_t>>> channel_matrix(channels_cnt);
    std::vector<ChannelMetadata> channel_metadata(channels_cnt);
    for (uint8_t c = 0; c < channels_cnt; ++c) {
        const uint8_t channel_id = channel_ids[c];
        channel_metadata[c] = meta.GetMetaByChannelId(channel_id);
        const size_t h = channel_metadata[c].h, v = channel_metadata[c].v;
        channel_matrix[c].reserve(h * v * mcu_h * mcu_w);
    }
    for (uint16_t mcu_y = 0; mcu_y < mcu_h; ++mcu_y) {
        for (uint16_t mcu_x = 0; mcu_x < mcu_w; ++mcu_x) {
            for (uint8_t c = 0; c < channels_cnt; ++c) {
                const auto& channel_meta = channel_metadata[c];
                const uint8_t h = channel_meta.h, v = channel_meta.v;

                for (uint8_t block_v = 0; block_v < v; ++block_v) {
                    for (uint8_t block_h = 0; block_h < h; ++block_h) {
                        auto block = ReadBlock(dc_trees[c], ac_trees[c], prev_dc[c]);
                        channel_matrix[c].push_back(std::move(block));
                    }
                }
            }
        }
    }

    // DLOG(INFO) << "Finished reading image data\n";
    return ImageData(std::move(channel_matrix), channel_ids, mcu_h, mcu_w);
}
