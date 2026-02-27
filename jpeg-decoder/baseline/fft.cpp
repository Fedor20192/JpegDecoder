#include <glog/logging.h>
#include <cmath>
#include <fft.h>

#include <fftw3.h>

class DctCalculator::Impl {
public:
    Impl(const size_t width, std::vector<double> *input, std::vector<double> *output)
        : width_(width), input_(input->data()), output_(output->data()) {
    }

    void Inverse() const {
        const double sqrt_two = std::sqrt(2);
        for (size_t i = 0; i < width_ * width_; ++i) {
            input_[i] /= 16.0;
            if (i < width_) {
                input_[i] *= sqrt_two;
            }
            if (i % width_ == 0) {
                input_[i] *= sqrt_two;
            }
        }
        const auto p = fftw_plan_r2r_2d(width_, width_, input_, output_, FFTW_REDFT01, FFTW_REDFT01,
                                        FFTW_ESTIMATE);
        fftw_execute(p);
        fftw_destroy_plan(p);
    }
    size_t width_;
    double *input_, *output_;
};

DctCalculator::DctCalculator(size_t width, std::vector<double> *input,
                             std::vector<double> *output) {
    if (input == nullptr || output == nullptr) {
        DLOG_IF(ERROR, input == nullptr) << "input is nullptr\n";
        DLOG_IF(ERROR, output == nullptr) << "output is nullptr\n";
        throw std::invalid_argument("null input/output");
    }
    if (input->size() != width * width || output->size() != width * width) {
        DLOG_IF(ERROR, input->size() != width * width)
            << input->size() << " != " << width << " * " << width << '\n';
        DLOG_IF(ERROR, output->size() != width * width)
            << output->size() << " != " << width << " * " << width << '\n';
        throw std::invalid_argument("input/output->sz != width * width");
    }
    impl_ = std::make_unique<Impl>(width, input, output);
}

void DctCalculator::Inverse() {
    impl_->Inverse();
}

DctCalculator::~DctCalculator() = default;
