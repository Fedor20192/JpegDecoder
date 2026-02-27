#include <cmath>
#include <decoder.h>
#include <glog/logging.h>

#include "fft.h"
#include "parsers.h"

std::vector<int16_t> Mult(std::vector<int16_t> one, const std::vector<uint16_t> &two) {
    if (one.size() != two.size()) {
        // DLOG(ERROR) << "Cannot multiply on quantum matrix\n"
        //             << one.size() << " != " << two.size() << '\n';
        throw std::runtime_error("Cannot multiply on quantum matrix");
    }
    for (size_t i = 0; i < one.size(); ++i) {
        one[i] *= two[i];
    }
    return one;
}

RGB YCbCrToRGB(const std::vector<int16_t> &channels) {
    if (channels.empty()) {
        // DLOG(ERROR) << "Channels is empty\n";
        throw std::invalid_argument("Channels is empty");
    }

    long double y = channels[0], cr = 128.0, cb = 128.0;
    if (channels.size() > 1) {
        cb = channels[1];
    }
    if (channels.size() > 2) {
        cr = channels[2];
    }
    if (channels.size() > 3) {
        // DLOG(WARNING) << "Too much channels\n";
    }

    RGB ans;

    long double r = y + 1.402 * (cr - 128.0);
    long double g = y - 0.344136 * (cb - 128.0) - 0.714136 * (cr - 128.0);
    long double b = y + 1.772 * (cb - 128.0);

    ans.r = static_cast<uint8_t>(
        std::llround(std::max<long double>(0.0, std::min<long double>(255.0, r))));
    ans.g = static_cast<uint8_t>(
        std::llround(std::max<long double>(0.0, std::min<long double>(255.0, g))));
    ans.b = static_cast<uint8_t>(
        std::llround(std::max<long double>(0.0, std::min<long double>(255.0, b))));
    return ans;
}

void Quantization(const RawImage &raw_image, ImageData &image_data) {
    const auto &meta = raw_image.metadata;
    const auto &quantum_tables = raw_image.quantum_tables;
    const size_t channels_cnt = image_data.channel_ids.size();

    for (size_t i = 0; i < channels_cnt; ++i) {
        const auto channel_id = image_data.channel_ids[i];
        const auto &channel_meta = meta.GetMetaByChannelId(channel_id);
        const auto &quantum_table = quantum_tables.at(channel_meta.quant_id).data;
        auto &channels_matrix = image_data.channel_matrix[i];
        for (size_t j = 0; j < channels_matrix.size(); ++j) {
            channels_matrix[j] = Mult(channels_matrix[j], quantum_table);
        }
    }
}

void IDCT(ImageData &image_data) {
    const size_t channels_cnt = image_data.channel_ids.size();
    for (size_t i = 0; i < channels_cnt; ++i) {
        auto &channels_matrix = image_data.channel_matrix[i];
        for (size_t j = 0; j < channels_matrix.size(); ++j) {
            std::vector<double> input_arr(channels_matrix[j].size());
            for (size_t k = 0; k < channels_matrix[j].size(); ++k) {
                input_arr[k] = static_cast<double>(channels_matrix[j][k]);
            }
            std::vector<double> output(input_arr.size());
            auto calc = DctCalculator(8, &input_arr, &output);
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
    for (size_t i = 0; i < channels_cnt; ++i) {
        h_max = std::max(h_max, meta.channels[i].h);
        v_max = std::max(v_max, meta.channels[i].v);
    }

    std::vector y_cb_cr(meta.height, std::vector(meta.width, std::vector<int16_t>(channels_cnt)));
    const uint16_t mcu_h_sz = 8 * v_max, mcu_w_sz = 8 * h_max;
    std::vector<size_t> now_block(channels_cnt, 0);
    for (size_t mcu_y = 0; mcu_y < image_data.mcu_h; ++mcu_y) {
        for (uint16_t mcu_x = 0; mcu_x < image_data.mcu_w; ++mcu_x) {
            const uint16_t mcu_y_start = mcu_y * mcu_h_sz, mcu_x_start = mcu_x * mcu_w_sz;

            for (size_t c = 0; c < channels_cnt; ++c) {
                const uint8_t channel_id = image_data.channel_ids[c];
                const auto &channel_meta = meta.GetMetaByChannelId(channel_id);
                const uint8_t h = channel_meta.h, v = channel_meta.v;

                for (size_t block_v = 0; block_v < v; ++block_v) {
                    for (size_t block_h = 0; block_h < h; ++block_h) {
                        const auto &block = image_data.channel_matrix[c][now_block[c]++];
                        const size_t block_y_start = mcu_y_start + block_v * 8 * (v_max / v);
                        const size_t block_x_start = mcu_x_start + block_h * 8 * (h_max / h);

                        for (uint8_t local_y = 0; local_y < 8; ++local_y) {
                            for (uint8_t local_x = 0; local_x < 8; ++local_x) {
                                const size_t value = block[local_y * 8 + local_x];
                                const size_t real_y = block_y_start + local_y * (v_max / v);
                                const size_t real_x = block_x_start + local_x * (h_max / h);

                                for (size_t delta_y = 0; delta_y < v_max / v; ++delta_y) {
                                    for (size_t delta_x = 0; delta_x < h_max / h; ++delta_x) {
                                        if (real_y + delta_y < meta.height &&
                                            real_x + delta_x < meta.width) {
                                            y_cb_cr[real_y + delta_y][real_x + delta_x][c] = value;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (size_t y = 0; y < meta.height; ++y) {
        for (size_t x = 0; x < meta.width; ++x) {
            ans.SetPixel(y, x, YCbCrToRGB(y_cb_cr[y][x]));
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

    IDCT(image_data);

    Rationing(image_data);

    GetAns(image_data, meta, ans);
    return ans;
}
