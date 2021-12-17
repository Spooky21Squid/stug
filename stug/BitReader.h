#include <vector>

// A class to read a single bit at a time from an unsigned char vector
class BitReader {
    private:
        std::vector<unsigned char> &data_;
        unsigned int currentByte_;
        unsigned int currentBit_;
        bool read_;
    public:
        BitReader(std::vector<unsigned char> &data, unsigned int startByte = 0);

        // Read the next n bits from data. If n is greater than the amount of remaining bits, only the remaining bits
        // will be returned. If there are no more remaining bits, 0 will be returned
        unsigned int next(unsigned int n = 1);

        // Returns whether the Bitreader has read all the bits in the vector
        bool read();

        // Skips the rest of the bits in the current byte. Next read operation will start from MSB of the next byte
        void skipToNextByte();
};

