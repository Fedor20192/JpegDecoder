#include <glog/logging.h>
#include <fft.h>
#include <fftw3.h>

class DctCalculator::Impl {
public:
    Impl(const size_t width, std::vector<double> *input, std::vector<double> *output)
        : width_(width), input_(input->data()), output_(output->data()) {
        plan_ = fftw_plan_r2r_2d(width_, width_, input_, output_, FFTW_REDFT01, FFTW_REDFT01,
                                 FFTW_ESTIMATE);
    }

    void Inverse() const {
        for (size_t i = 0; i < width_ * width_; ++i) {
            constexpr double kSqrtTwo = 1.4142135623730951;
            input_[i] /= 16.0;
            if (i < width_) {
                input_[i] *= kSqrtTwo;
            }
            if (i % width_ == 0) {
                input_[i] *= kSqrtTwo;
            }
        }

        fftw_execute(plan_);
    }

    ~Impl() {
        fftw_destroy_plan(plan_);
    }

private:
    fftw_plan plan_;
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
