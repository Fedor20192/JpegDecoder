#include "include/huffman.h"

#include <memory>
#include <numeric>
#include <optional>
#include <glog/logging.h>

std::ostream &operator<<(std::ostream &of, const std::vector<uint8_t> &vec) {
    for (const auto val : vec) {
        of << static_cast<int>(val) << ' ';
    }
    return of;
}

class HuffmanTree::Impl {
public:
    Impl() = default;

    bool AddNode(const uint8_t code_length, uint8_t value) const {
        return AddNodeImpl(code_length, value, root_);
    }

    void Reset() {
        root_ = state_ = std::make_shared<Node>();
    }

    bool Move(bool bit, int &value) {
        if (state_ == nullptr) {
            throw std::invalid_argument("State is nullptr");
        }
        // DLOG(INFO) << "Trying to move by " << bit << '\n';

        std::shared_ptr<Node> need;
        if (!bit) {
            need = state_->left;
        } else {
            need = state_->right;
        }

        state_ = need;
        if (need == nullptr) {
            // DLOG(ERROR) << "You are trying to move in nullptr\n";
            return false;
        }
        if (!need->is_terminal) {
            return false;
        }
        state_ = root_;
        value = need->value.value();
        // DLOG(INFO) << "Moved in " << value << '\n';
        return true;
    }

private:
    struct Node {
        explicit Node(std::optional<uint8_t> value = std::nullopt, bool is_terminal = false)
            : is_terminal(is_terminal), value(value) {
        }

        bool is_terminal;
        std::optional<uint8_t> value;
        std::shared_ptr<Node> left{nullptr}, right{nullptr};
    };

    std::shared_ptr<Node> root_, state_;

    static bool AddNodeImpl(uint8_t code_length, uint8_t value, const std::shared_ptr<Node> &now) {
        if (now->is_terminal) {
            return false;
        }
        if (code_length == 1) {
            if (now->left == nullptr) {
                now->left = std::make_shared<Node>(value, true);
                return true;
            }
            if (now->right == nullptr) {
                now->right = std::make_shared<Node>(value, true);
                return true;
            }
            return false;
        }
        if (now->left == nullptr) {
            now->left = std::make_shared<Node>();
        }
        if (AddNodeImpl(code_length - 1, value, now->left)) {
            return true;
        }
        if (now->right == nullptr) {
            now->right = std::make_shared<Node>();
        }
        if (AddNodeImpl(code_length - 1, value, now->right)) {
            return true;
        }
        return false;
    }
};

HuffmanTree::HuffmanTree() : impl_(std::make_unique<Impl>()) {
}

void HuffmanTree::Build(const std::vector<uint8_t> &code_lengths,
                        const std::vector<uint8_t> &values) {
    // DLOG(INFO) << "Start building Huffman Tree\n";

    impl_->Reset();
    std::vector<uint8_t> code_lengths_new = code_lengths;
    size_t now_length = 1;
    for (const uint8_t value : values) {
        while (code_lengths_new[now_length - 1] == 0 && now_length <= code_lengths.size()) {
            ++now_length;
        }
        if (now_length > std::min<size_t>(16, code_lengths.size())) {
            // DLOG(ERROR) << "Bad code lengths\nCodes lengths: " << code_lengths
            //             << "\nValues cnt: " << values.size() << '\n';
            throw std::invalid_argument("Too big code length");
        }
        --code_lengths_new[now_length - 1];
        if (!impl_->AddNode(now_length, value)) {
            // DLOG(ERROR) << "Cannot add a node\nCodes lengths: " << code_lengths
            //             << "\nValues cnt: " << values.size() << '\n';
            throw std::invalid_argument("Something went wrong in node adding");
        }
    }

    if (std::reduce(code_lengths_new.begin(), code_lengths_new.end()) > 0) {
        // DLOG(ERROR) << "Big lenghts sum\nCodes lengths: " << code_lengths
        //             << "\nValues cnt: " << values.size() << '\n';
        throw std::invalid_argument("Too big code length sum");
    }

    // DLOG(INFO) << "Finished building Huffman Tree\n";
}

bool HuffmanTree::Move(bool bit, int &value) {
    return impl_->Move(bit, value);
}

HuffmanTree::HuffmanTree(HuffmanTree &&) = default;

HuffmanTree &HuffmanTree::operator=(HuffmanTree &&) = default;

HuffmanTree::~HuffmanTree() = default;
