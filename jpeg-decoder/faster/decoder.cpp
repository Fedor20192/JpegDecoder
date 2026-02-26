#include <cmath>
#include <decoder.h>
#include <glog/logging.h>

#include "fft.h"
#include "parsers.h"

void Mult(std::vector<int16_t> &one, const std::vector<uint16_t> &two) {
    if (one.size() != two.size()) {
        // DLOG(ERROR) << "Cannot multiply on quantum matrix\n"
        //             << one.size() << " != " << two.size() << '\n';
        throw std::runtime_error("Cannot multiply on quantum matrix");
    }
    for (size_t i = 0; i < one.size(); ++i) {
        one[i] *= two[i];
    }
}

RGB YCbCrToRGB(const std::vector<int16_t> &channels) {
    if (channels.empty()) {
        // DLOG(ERROR) << "Channels is empty\n";
        throw std::invalid_argument("Channels is empty");
    }

    const int y = channels[0] << 10;
    const int cb = (channels.size() > 1) ? channels[1] - 128 : 0;
    const int cr = (channels.size() > 2) ? channels[2] - 128 : 0;

    const int r_ans = y + 1402 * cr;
    const int g_ans = y - 344 * cb - 714 * cr;
    const int b_fp = y + 1772 * cb;

    auto clamp8 = [](const int v_fp) -> uint8_t {
        const int v = v_fp >> 10;
        if (v < 0) {
            return 0;
        }
        if (v > 255) {
            return 255;
        }
        return static_cast<uint8_t>(v);
    };

    RGB ans;
    ans.r = clamp8(r_ans);
    ans.g = clamp8(g_ans);
    ans.b = clamp8(b_fp);
    return ans;
}

void Quantization(const RawImage &raw_image, ImageData &image_data) {
    const auto &meta = raw_image.metadata;
    const auto &quantum_tables = raw_image.quantum_tables;
    const size_t channels_cnt = image_data.channel_ids.size();

    for (size_t i = 0; i < channels_cnt; ++i) {
        const auto channel_id = image_data.channel_ids[i];
        const auto &channel_meta = meta.GetMetaByChannelId(channel_id);
        const auto &quantum_table = quantum_tables[channel_meta.quant_id].value().data;
        auto &channels_matrix = image_data.channel_matrix[i];
        for (size_t j = 0; j < channels_matrix.size(); ++j) {
            Mult(channels_matrix[j], quantum_table);
        }
    }
}

void IDCT(ImageData &image_data) {
    const size_t channels_cnt = image_data.channel_ids.size();
    std::vector<double> input_arr(64), output(64);
    auto calc = DctCalculator(8, &input_arr, &output);
    for (size_t i = 0; i < channels_cnt; ++i) {
        auto &channels_matrix = image_data.channel_matrix[i];
        for (size_t j = 0; j < channels_matrix.size(); ++j) {
            for (size_t k = 0; k < channels_matrix[j].size(); ++k) {
                input_arr[k] = static_cast<double>(channels_matrix[j][k]);
            }
            calc.Inverse();
            for (size_t k = 0; k < output.size(); ++k) {
                channels_matrix[j][k] = static_cast<int16_t>(std::round(output[k]));
            }
        }
    }
}

void Rationing(ImageData &image_data) {
    const size_t channels_cnt = image_data.channel_ids.size();
    for (size_t i = 0; i < channels_cnt; ++i) {
        auto &channels_matrix = image_data.channel_matrix[i];
        for (size_t j = 0; j < channels_matrix.size(); ++j) {
            for (size_t k = 0; k < channels_matrix[j].size(); ++k) {
                channels_matrix[j][k] =
                    std::min<int16_t>(std::max<int16_t>(0, channels_matrix[j][k] + 128), 255);
            }
        }
    }
}

void GetAns(const ImageData &image_data, const ImageMetadata &meta, Image &ans) {
    const size_t channels_cnt = image_data.channel_ids.size();
    uint8_t h_max = 0, v_max = 0;
    std::vector<ChannelMetadata> channel_metadata(channels_cnt);
    std::vector<std::pair<uint16_t, uint16_t>> scaling(channels_cnt);
    for (size_t c = 0; c < channels_cnt; ++c) {
        h_max = std::max(h_max, meta.channels[c].h);
        v_max = std::max(v_max, meta.channels[c].v);
        const uint8_t channel_id = image_data.channel_ids[c];
        channel_metadata[c] = meta.GetMetaByChannelId(channel_id);
        scaling[c] = std::make_pair(v_max / channel_metadata[c].v, h_max / channel_metadata[c].h);
    }

    std::vector buffer(channels_cnt, std::vector<int16_t>(64 * h_max * v_max));
    const uint16_t mcu_h_sz = 8 * v_max, mcu_w_sz = 8 * h_max;
    std::vector<size_t> now_block(channels_cnt, 0);
    std::vector<int16_t> channels_values(channels_cnt);
    for (size_t mcu_y = 0; mcu_y < image_data.mcu_h; ++mcu_y) {
        for (uint16_t mcu_x = 0; mcu_x < image_data.mcu_w; ++mcu_x) {
            const uint16_t mcu_y_start = mcu_y * mcu_h_sz, mcu_x_start = mcu_x * mcu_w_sz;

            for (size_t c = 0; c < channels_cnt; ++c) {
                const auto &channel_meta = channel_metadata[c];
                const uint8_t h = channel_meta.h, v = channel_meta.v;
                const auto &[v_scale, h_scale] = scaling[c];

                for (size_t block_v = 0; block_v < v; ++block_v) {
                    for (size_t block_h = 0; block_h < h; ++block_h) {
                        const auto &block = image_data.channel_matrix[c][now_block[c]++];
                        const size_t block_y_start = mcu_y_start + block_v * 8 * v_scale;
                        const size_t block_x_start = mcu_x_start + block_h * 8 * h_scale;

                        for (uint8_t local_y = 0; local_y < 8; ++local_y) {
                            for (uint8_t local_x = 0; local_x < 8; ++local_x) {
                                const size_t value = block[local_y * 8 + local_x];
                                const size_t real_y = block_y_start + local_y * v_scale;
                                const size_t real_x = block_x_start + local_x * h_scale;

                                for (size_t delta_y = 0; delta_y < v_scale; ++delta_y) {
                                    for (size_t delta_x = 0; delta_x < h_scale; ++delta_x) {
                                        buffer[c][(real_y + delta_y - mcu_y_start) * mcu_w_sz +
                                                  real_x + delta_x - mcu_x_start] = value;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            for (size_t delta_y = 0; delta_y < 8 * v_max; ++delta_y) {
                for (size_t delta_x = 0; delta_x < 8 * h_max; ++delta_x) {
                    const size_t ind = delta_y * mcu_w_sz + delta_x;
                    for (size_t c = 0; c < channels_cnt; ++c) {
                        channels_values[c] = buffer[c][ind];
                    }
                    const size_t y = mcu_y_start + delta_y;
                    const size_t x = mcu_x_start + delta_x;
                    if (y < meta.height && x < meta.width) {
                        ans.SetPixel(y, x, YCbCrToRGB(channels_values));
                    }
                }
            }
        }
    }
}

Image Decode(std::istream &input) {
    // DLOG(INFO) << "Starting decoder\n";
    auto parser = Parser(input);

    const auto raw_image = parser.ReadRawImage();

    const auto &meta = raw_image.metadata;

    Image ans(meta.width, raw_image.metadata.height);

    ans.SetComment(raw_image.comment);

    ImageData image_data = raw_image.data;

    Quantization(raw_image, image_data);
    ;

    IDCT(image_data);

    Rationing(image_data);

    GetAns(image_data, meta, ans);

    // DLOG(INFO) << "Finished decoder\n";
    return ans;
}
