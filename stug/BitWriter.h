#include <vector>

// A class to write a single bit at a time onto an unsigned char vector
class BitWriter {
    private:
        std::vector<unsigned char> data_;
        // The index of the next bit to be written to
        unsigned int nextBit_;
    public:
        BitWriter();

        // Writes a 1 or 0 onto the last element of the internal unsigned char vector. If a whole number of bytes has
        //  already been written, a new uchar will be added with the msb equal to the new bit. Any new uchar added will
        //  be equal to 0x00.
        void write(bool bit);

        // Writes an unsigned char in binary form to the end of the internal unsigned char vector. If a whole number of bytes
        //  has already been written, the char will simply be appended. If not, then the char is written starting from the
        //  next available bit in the last byte.
        void write(unsigned char c);

        // Writes a code to the data_ vector given the unsigned int representation of that code, and the length. Will write
        //  from the MSB of the code to the LSB.
        void write(unsigned int i, int length);

        // Pads the last byte's unwritten bits with zeros.
        void pad(bool bit);

        // Copies the internal data vector to the parameter data vector. If addPadding is true, a 0x00 char will be added
        //  after each 0xFF char.
        void copy(std::vector<unsigned char> &data, bool addPadding);
};

