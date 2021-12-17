#include "BitWriter.h"

BitWriter::BitWriter() {
    nextBit_ = 0;
};

void BitWriter::write(bool bit) {

    if (nextBit_ == 0) {
        data_.push_back(0x00);
    }

    if (bit) {
        data_.back() = data_.back() >> (7 - nextBit_);
        data_.back() = data_.back() | 0x01;
        data_.back() = data_.back() << (7 - nextBit_);
    }
    nextBit_ = (nextBit_ + 1) % 8;

};

void BitWriter::write(unsigned char c) {
    if (nextBit_ == 0) data_.push_back(c);
    else {
        bool b;
        for (int i = 0; i < 8; ++i) {
            b = (c >> (7 - i)) & 0x01;
            write(b);
        }
    }
}

void BitWriter::write(unsigned int i, int length) {
    for (int j = 0; j < length; ++j) {
        write((bool)((i >> length - j - 1) & 0x01));
    }
};

void BitWriter::copy(std::vector<unsigned char> &data, bool addPadding) {
    for (unsigned char &c : data_) {
        data.push_back(c);
        if (c == 0xFF) data.push_back(0x00);
    }
};

void BitWriter::pad(bool bit = false) {
    if (bit) {
        while (nextBit_ > 0) write(true);
    }
    else nextBit_ = 0;
}
