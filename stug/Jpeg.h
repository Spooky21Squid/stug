#include "BitReader.h"
#include "BitWriter.h"

#include <string>
#include <iostream>
#include <vector>

const std::vector<unsigned char> unsupportedMarkers{
    0xC1,  // Extended Sequential DCT
    0xC2,  // Progressive DCT
    0xC3,  // Lossless
    0xC5,  // Differential Sequential  DCT
    0xC6,  // Differential Progressive DCT
    0xC7,  // Differential Lossless
    0xC9,  // Extended Sequential DCT
    0xCA,  // Progressive DCT
    0xCB,  // Lossless (Sequential)
    0xCD,  // Differential Sequential DCT
    0xCE,  // Differential Progressive DCT
    0xCF,  // Differential Lossless
    0xCC,  // Arithmetic Coding
    0x01  // TEM merker - Arithmetic Coding
};

struct Component {
  unsigned int identifier = 0;
  unsigned int verticalSamplingFactor = 0;
  unsigned int horizontalSamplingFactor = 0;
  unsigned int quantizationTableNumber = 0;
  unsigned int acHuffmanTableId = 0;
  unsigned int dcHuffmanTableId = 0;
};

struct StartOfFrame {
  unsigned int dataPrecision = 0;
  unsigned int imageHeight = 0;
  unsigned int imageWidth = 0;
  unsigned int numberOfComponents = 0x00;
  bool set = false;
};

// Can be either a luminance or a chrominance channel
struct Channel {
  int dcCoefficient = 0;
  int acCoefficients[63] = { 0 };
};

// An MCU can contain up to 4 Y blocks (if quarter-sub sampled) and a Cb and Cr block
struct MCU {
  Channel luminance[4];
  Channel chrominance[2];
};

struct StartOfScan {
  bool set = false;
};

struct HuffmanTable {
  unsigned char symbols[162] = { 0x00 };
  unsigned int offsets[17] = { 0 };
  unsigned int codes[162] = { 0 };
  bool set = false;
};

// Sets all values inside ht to represent a standard jpeg huffman table, depending on the type and component.
// type can be either "dc" or "ac". component can be either "lum" or "chr", for luminance or chrominance.
void createStandardHuffmanTable(HuffmanTable &ht, std::string type, std::string component);

class Header {
  private:
    StartOfScan startOfScan_;
    StartOfFrame startOfFrame_;
    Component components[3];  // Y, Cb and Cr components
    HuffmanTable dcHuffmanTables_[2];  // Baseline DHT only allows table destination identifiers equal to 1 or 0 (2 tables for ac and2 for dc)
    HuffmanTable acHuffmanTables_[2];
    unsigned int restartInterval_ = 0;  // The amount of MCUs in between each restart marker
    unsigned int bitstreamIndex_ = 0;  // The first byte of the huffman coded bitstream
    std::vector<unsigned char> quantizationTables;
    std::vector<unsigned char> app0Marker;

    // Reads the start of frame data from fileData, from start to end, and updates the start of frame object
    void readStartOfFrame(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length);

    // Reads the start of scan data from fileData, from start to end, and updates the start of scan object
    void readStartOfScan(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length);

    // Reads the define restart interval, from the start, and updates the header object
    void readDefineRestartInterval(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length);

    // Reads the huffman table data from fileData, from start to end, and updates the huffman table object
    void readHuffmanTable(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length);

    // Reads the length of the marker, starting at the first length byte
    unsigned int getMarkerLength(std::vector<unsigned char> &fileData, unsigned int start);

    // Converts an unsigned char (a symbol) into a code and length. Which huffman table to use is specified by
    //  bool parameters 'dc' and 'lum', ac = true meaning a AC table, chr = true meaning a Chrominance table, and vice versa for false.
    void convertSymbolToCode(unsigned char symbol, bool ac, bool chr, unsigned int &code, int &length);

    // Fills the header vector with the bytestream representation of this header
    void createHeaderBytes(std::vector<unsigned char> &header);

    // Adds Start of Frame marker to header vector
    void addStartOfFrame(std::vector<unsigned char> &header);

    // Adds Quantization table marker to header vector
    void addQuantizationTables(std::vector<unsigned char> &header);

    // Adds start of scan marker to header vector
    void addStartOfScan(std::vector<unsigned char> &header);

    // Adds the sequence of bytes that contain the quantization table info to the header
    void readQuantizationTables(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length);

    // Reads the APP0 marker used to identify JFIF files
    void readApp0Marker(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length);

    // Adds the APP0 marker to the header vector
    void addApp0Marker(std::vector<unsigned char> &header);

    // Adds DHT marker to header vector. tableClass can be 0 for DC or 1 for AC. tableID can be 0 for Luminance, or 1
    //  for Chrominance.
    void addHuffmanTable(std::vector<unsigned char> &header, unsigned int tableClass, unsigned int tableID);

    // Analyses the in-memory Jpeg file data and stores it metadata within this Header object
    void readHeader(std::vector<unsigned char> &fileData);
  public:
    friend class Jpeg;

    friend std::ostream& operator<<(std::ostream& os, const Header& h);
};

class Jpeg {
  private:
    Header header_;
    std::vector<MCU> MCUArray_;
    int currentMCU;
    int currentChannel;
    bool currentChannelType;  // true for luminance, false for chrominance
    int currentCoefficient;
    unsigned int freeBits;

    // Returns a pointer to the next coefficient that can be used for secret writing
    int * getNextFreeCoefficient();

    // Returns a pointer to the next coefficient in the MCU array
    int * getNextCoefficient();

    // Reads the huffman-coded bitstream within the fileData using the information inside the Header object, and populates the MCU array with it
    void readBitstream(std::vector<unsigned char> &fileData);

    // Reads the next MCU from the bitstream contained within BitReader, according to rules within the Header object
    void readNextMCU(MCU& m, BitReader &b, int lastDcCoefficient);

    // Reads the next 8x8 Y, Cb or Cr block from the bitstream
    void readBlock(Channel &c, BitReader &b, int lastDcCoefficient, HuffmanTable &dc, HuffmanTable &ac);

    // Reads the next symbol from the bitstream using the Huffman Table
    unsigned char getNextSymbol(BitReader &b, HuffmanTable &ht);

    // Gets secret data from Jpeg and stores in secretData
    void retrieveFromJpeg(std::vector<unsigned char> &secretData);

    // Creates a new huffman-coded bitstream from the MCU Array using standard huffman tables
    void createNewBitstream(std::vector<unsigned char> &bitstream);

    // Turns a single MCU into a bitstream segment given the huffman tables to be used, and appends it to bitstream
    void addMCUToBitstream(MCU &m, BitWriter &bw);

    // Turns a single block into a bitstream segment given the huffman tables to be used, and appends it to bitstream
    void addBlockToBitstream(Channel &channel, BitWriter &bw, bool chr);

    // Resets the current coefficient position
    void resetCurrent();
  public:
    // Opens a Jpeg file and saves its details into the Jpeg object
    Jpeg(std::string filename);

    // Hides a file, given by 'filename', in the Jpeg as a binary file
    void hide(std::string filename);

    // Saves the Jpeg in its current form to the current directory
    void save(std::string name);

    // Recovers a hidden file stored within Jpeg and saves it to the current directory
    void recover();
};

// Loads the JPEG file into memory, as an array of unsigned chars representing the bytes in the file
std::vector<unsigned char> loadFile(std::string filename);

// Populates the huffman table's codes array with codes of increasing lengths, using the symbols and offsets arrays
void getHuffmanCodes(HuffmanTable *ht);

std::ostream& operator<<(std::ostream& os, const Header& h);

// Returns a string representation of code of length 'size'
std::string getCodeAsBinary(unsigned int size, unsigned int code);

// Modifies fileData to include fileName, and the number of bytes of fileData, to prepare it for hiding inside the Jpeg
void prepareFileForHiding(std::vector<unsigned char> &fileData, std::string fileName);

// Extracts filename from the recovered hidden bytes and separates it from the file data
std::string separateFileNameFromFileData(std::vector<unsigned char> &fileData);

// Saves the secret file to the current directory
void saveFile(std::string &fileName, const std::vector<unsigned char> &fileData);

// Returns the minimum number of bits that can be used to represent the number. E.g, to represent +4 in binary,
//  3 bits (100) need to be used. If number is negative, the minimum length of the one's complement of that number
//  will be returned. E.g, to represent -4, 3 bits again will be used. if number is 0, returns 0.
int getMinimumBinaryLength(int number);