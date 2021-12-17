#include "BitReader.h"

BitReader::BitReader(std::vector<unsigned char> &data, unsigned int startByte) : data_(data), currentByte_(startByte) {
    currentBit_ = 0;
    read_ = false;
}

unsigned int BitReader::next(unsigned int n) {
    unsigned int result = 0;
    unsigned char currentByte;
    for (int i = 0; i < n; ++i) {
        if (read_) break;
        currentByte = data_[currentByte_];
        currentByte = currentByte >> (7 - currentBit_);
        result = result << 1;
        result = result | (currentByte & 0x01);

        currentBit_ = (currentBit_ + 1) % 8;
        if (currentBit_ == 0) currentByte_ += 1;

        if (currentByte_ >= data_.size()) read_ = true;
    }
    return result;
}

bool BitReader::read() {
    return read_;
}

void BitReader::skipToNextByte() {
    if (currentByte_ != data_.size() - 1) {
        ++currentByte_;
        currentBit_ = 0;
    }
}
