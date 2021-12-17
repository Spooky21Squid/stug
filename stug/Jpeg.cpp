#include "Jpeg.h"

#include <iostream>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

std::vector<unsigned char> loadFile(std::string filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        throw std::runtime_error("Could not open file.");
    }

    char* signedFileData;
    std::streampos fileSize;

    fileSize = file.tellg();
    signedFileData = new char[fileSize];
    file.seekg(0, std::ios::beg);
    file.read(signedFileData, fileSize);
    file.close();

    std::vector<unsigned char> bytes;

    for (int i = 0; i < fileSize; ++i) {
        bytes.push_back(static_cast<unsigned char>(signedFileData[i]));
    }

    return bytes;
}

void Header::readHeader(std::vector<unsigned char> &fileData) {
    // go through each byte. if it is a readable marker, read it. if it isnt a readable marker, skip it. If its an invalid
    //  marker (eg a progressive sof) throw error.
    unsigned char currentByte;

    for (int i = 0; i < fileData.size(); ++i) {
        if (startOfScan_.set) {
            bitstreamIndex_ = i;
            break;
        }
        currentByte = fileData[i];
        if (currentByte != 0xFF) continue;
        currentByte = fileData[++i];
        if (currentByte == 0xD8 || currentByte == 0xD9 || currentByte == 0x01 || currentByte == 0xFF) continue;  // Markers with no length or extra FF byte
        unsigned int markerLength = getMarkerLength(fileData, ++i);

        if (currentByte == 0xC0) readStartOfFrame(fileData, i, markerLength);
        else if (currentByte == 0xDA) readStartOfScan(fileData, i, markerLength);
        else if (currentByte == 0xDD) readDefineRestartInterval(fileData, i, markerLength);
        else if (currentByte == 0xC4) readHuffmanTable(fileData, i, markerLength);
        else if (currentByte == 0xDB) readQuantizationTables(fileData, i, markerLength);
        else if (currentByte == 0xE0) readApp0Marker(fileData, i, markerLength);
        else if (std::find(unsupportedMarkers.begin(), unsupportedMarkers.end(), currentByte) != unsupportedMarkers.end()) {
            std::stringstream ss;
            ss << "Unsupported marker (" << std::hex << (int) currentByte << ") found in Jpeg";
            throw std::runtime_error(ss.str());
        }
        i += markerLength - 1;
    }
}

void Header::readApp0Marker(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length) {
    for (int i = start; i < start + length; ++i) {
        app0Marker.push_back(fileData[i]);
    }
}

void Jpeg::readBitstream(std::vector<unsigned char> &fileData) {
    // Remove padding after FF bytes
    std::vector<unsigned char> scan;
    for (int i = header_.bitstreamIndex_; i < fileData.size(); ++i) {
        scan.push_back(fileData[i]);
        if (fileData[i] == 0xFF) {
            if (fileData[i+1] == 0x00) ++i;
        }
    }

    unsigned int blockWidth = ((header_.startOfFrame_.imageWidth + 7) / 8);  // Add an extra block to pad any missing pixels
    unsigned int blockHeight = ((header_.startOfFrame_.imageHeight + 7) / 8);
    if (blockWidth % 2 == 1  && header_.components[0].horizontalSamplingFactor == 2) ++blockWidth;  // Add another block if the number is odd
    if (blockHeight % 2 == 1 && header_.components[0].verticalSamplingFactor == 2) ++blockHeight;
    unsigned int numberOfBlocks = blockHeight * blockWidth;  // The effective number of blocks
    unsigned int numberOfMCUs = numberOfBlocks / (header_.components[0].verticalSamplingFactor * header_.components[0].horizontalSamplingFactor);
    int lastDcCoefficient;

    BitReader b(scan);

    for (int i = 0; i < numberOfMCUs; ++i) {
        MCU m;
        if (MCUArray_.size() == 0) lastDcCoefficient = 0;
        else lastDcCoefficient = MCUArray_.back().chrominance[1].dcCoefficient;
        readNextMCU(m, b, lastDcCoefficient);
        MCUArray_.push_back(m);
    }
};

void Header::readStartOfFrame(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length) {
    unsigned int i = start + 2;

    if (fileData[i] != 0x08) throw std::runtime_error("Invalid data precision");
    startOfFrame_.dataPrecision = (unsigned int) fileData[i];

    startOfFrame_.imageHeight = startOfFrame_.imageHeight | fileData[++i];
    startOfFrame_.imageHeight = startOfFrame_.imageHeight << 8;
    startOfFrame_.imageHeight = startOfFrame_.imageHeight | fileData[++i];

    startOfFrame_.imageWidth = startOfFrame_.imageWidth | fileData[++i];
    startOfFrame_.imageWidth = startOfFrame_.imageWidth << 8;
    startOfFrame_.imageWidth = startOfFrame_.imageWidth | fileData[++i];

    if (fileData[++i] != 0x01 && fileData[i] != 0x03) throw std::runtime_error("Invalid number of components");
    startOfFrame_.numberOfComponents = (unsigned int) fileData[i];

    for (int j = 0; j < startOfFrame_.numberOfComponents; ++j) {
        Component* component = &components[j];
        component->identifier = fileData[++i];
        component->horizontalSamplingFactor = fileData[++i] >> 4;
        component->verticalSamplingFactor = fileData[i] & 0x0F;
        component->quantizationTableNumber = fileData[++i];
    }

    // Check if the sampling factor of components is supported
    if (startOfFrame_.numberOfComponents > 1) {
        if ((components[0].verticalSamplingFactor != 1 && components[0].verticalSamplingFactor != 2)
            || (components[0].horizontalSamplingFactor != 1 && components[0].horizontalSamplingFactor != 2)
            || (components[1].horizontalSamplingFactor != 1 || components[1].verticalSamplingFactor != 1)
            || (components[2].horizontalSamplingFactor != 1 || components[2].verticalSamplingFactor != 1)) {
                throw std::runtime_error("Sampling factor is not supported");
        }
    } else {
        // If there is only one component, set the sampling factor to 1x1
        components[0].verticalSamplingFactor = 1;
        components[0].horizontalSamplingFactor = 1;
    }

    if (i != start + length - 1) throw std::runtime_error("SOF length did not match total bytes read");
    startOfFrame_.set = true;
};

void Header::readStartOfScan(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length) {
    unsigned int i = start + 2;
    unsigned char currentByte;

    if (fileData[i] != startOfFrame_.numberOfComponents) throw std::runtime_error("Number of components in Start of Scan does not match number of components in Start of Frame");
    
    unsigned int componentId;
    for (int j = 0; j < startOfFrame_.numberOfComponents; ++j) {
        Component* component = &components[j];
        componentId = fileData[++i];
        if (componentId != component->identifier) throw std::runtime_error("Component ID in Start of Scan does not match component ID in Start of Frame");  // new
        component->acHuffmanTableId = fileData[++i] & 0x0F;
        component->dcHuffmanTableId = fileData[i] >> 4;
    }

    i+=3;
    if (i != start + length - 1) throw std::runtime_error("Number of bytes in Start of Scan does not match length of marker");
    startOfScan_.set = true;
};

void Header::readQuantizationTables(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length) {
    quantizationTables.push_back(0xFF);
    quantizationTables.push_back(0xDB);
    for (int i = start; i < start + length; ++i) quantizationTables.push_back(fileData[i]);
}

void Header::readHuffmanTable(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length) {
    unsigned int i = start + 2;
    unsigned char currentByte;
    HuffmanTable* ht;

    while (i < start + length) {
        currentByte = fileData[i];
        if ((currentByte >> 4) > 1) throw std::runtime_error("Invalid Huffman Table Class");
        if ((currentByte & 0x0F) > 1) throw std::runtime_error("Invalid Huffman Table Destination Identifier");
        if ((currentByte >> 4) == 0) ht = &dcHuffmanTables_[currentByte & 0x0F];
        else ht = &acHuffmanTables_[currentByte & 0x0F];
        if (ht->set) throw std::runtime_error("Huffman Table was assigned more than once");

        unsigned int totalCodes = 0;
        for (int j = 1; j <= 16; ++j) {
            totalCodes += (int) fileData[++i];
            ht->offsets[j] = totalCodes;
        }

        for (int j = 0; j < totalCodes; ++j) ht->symbols[j] = fileData[++i];
        ht->set = true;
        getHuffmanCodes(ht);
        ++i;
    }
};

unsigned int Header::getMarkerLength(std::vector<unsigned char> &fileData, unsigned int start) {
    unsigned int markerLength = 0;
    unsigned int current = start;
    markerLength = markerLength | fileData[current++];
    markerLength = markerLength << 8;
    markerLength = markerLength | fileData[current];
    return markerLength;
};

void Header::readDefineRestartInterval(std::vector<unsigned char> &fileData, unsigned int start, unsigned int length) {
    if (length != 4) throw std::runtime_error("Length of Restart Interval Marker is not 4");
    unsigned int i = start + 2;

    restartInterval_ = restartInterval_ | fileData[i];
    restartInterval_ = restartInterval_ << 8;
    restartInterval_ = restartInterval_ | fileData[++i];
};

void getHuffmanCodes(HuffmanTable *ht) {
    unsigned int code = 0;
    for (unsigned int offset = 0; offset < 16; ++offset) {
        for (unsigned int n = ht->offsets[offset]; n < ht->offsets[offset + 1]; ++n) {
            ht->codes[n] = code;
            code += 1;
        }
        code = code << 1;
    }
}

std::ostream& operator<<(std::ostream& os, const Header& h) {
    if (h.startOfFrame_.set) {
        os << "Start Of Frame:\n\nData precision: " << h.startOfFrame_.dataPrecision << "\n";
        os << "Image height: " << h.startOfFrame_.imageHeight << "\n";
        os << "Image width: " << h.startOfFrame_.imageWidth << "\n";
        os << "Number of components: " << h.startOfFrame_.numberOfComponents << "\n";

        for (const Component &c : h.components) {
            os << "Component " << c.identifier << ":\n";
            os << "\tVertical sampling factor: " << (int) c.verticalSamplingFactor << "\n";
            os << "\tHorizontal sampling factor: " << (int) c.horizontalSamplingFactor << "\n";
            os << "\tQuantization table number: " << (int) c.quantizationTableNumber << "\n";
        }

        os << "\n";
    } else os << "Start Of Frame not set\n\n";

    os << "DC Huffman tables:\n\n";
    for (int i = 0; i < 2; ++i) {
        os << "DC Table " << i << ":\n";
        const HuffmanTable& ht = h.dcHuffmanTables_[i];
        if (!ht.set) os << " not set\n";
        else {
            unsigned int currentIndex = 0;
            for (int offset = 0; offset < 16; ++offset) {
                os << offset + 1 << ": " << std::hex;
                while (currentIndex < ht.offsets[offset + 1]) {
                    os << (int) ht.symbols[currentIndex];
                    os << " (" << getCodeAsBinary(offset + 1, ht.codes[currentIndex]) << ")";
                    os << ", ";
                    ++currentIndex;
                }
                os << std::dec << "\n";
            }
        }
        os << "\n";
    }

    os << "AC Huffman tables:\n\n";
    for (int i = 0; i < 2; ++i) {
        os << "AC Table " << i << ":\n";
        const HuffmanTable& ht = h.acHuffmanTables_[i];
        if (!ht.set) os << " not set\n";
        else {
            unsigned int currentIndex = 0;
            for (int offset = 0; offset < 16; ++offset) {
                os << offset + 1 << ": " << std::hex;
                while (currentIndex < ht.offsets[offset + 1]) {
                    os << (int) ht.symbols[currentIndex];
                    os << " (" << getCodeAsBinary(offset + 1, ht.codes[currentIndex]) << ")";
                    os << ", ";
                    ++currentIndex;
                }
                os << std::dec << "\n";
            }
        }
        os << "\n";
    }

    if (h.restartInterval_ == 0) os << "No restart interval.\n\n";
    else os << "Restart interval: " << h.restartInterval_ << "\n\n";

    if (h.startOfScan_.set) {
        os << "Start of Scan:\n\n";
        for (const Component &c : h.components) {
            os << "Component " << c.identifier << ":\n";
            os << "\tAC Huffman Table: " << c.acHuffmanTableId << "\n";
            os << "\tDC Huffman Table: " << c.dcHuffmanTableId << "\n";
        }
        os << "\n";
    } else os << "Start of Scan not set\n\n";

    return os;
}

std::string getCodeAsBinary(unsigned int size, unsigned int code) {
    std::string s = "";
    for (int i = 0; i < size; ++i) {
        if (code & 0x01 == 1) s = "1" + s;
        else s = "0" + s;
        code = code >> 1;
    }
    return s;
}

void Jpeg::readNextMCU(MCU& m, BitReader &b, int lastDcCoefficient) {
    unsigned int yBlocks = header_.components[0].horizontalSamplingFactor * header_.components[0].verticalSamplingFactor;
    
    for (int i = 0; i < yBlocks; ++i) {
        readBlock(m.luminance[i], b, lastDcCoefficient, header_.dcHuffmanTables_[header_.components[0].dcHuffmanTableId], header_.acHuffmanTables_[header_.components[0].acHuffmanTableId]);
        lastDcCoefficient = m.luminance[i].dcCoefficient;
    }

    if (header_.startOfFrame_.numberOfComponents > 1) {
        readBlock(m.chrominance[0], b, lastDcCoefficient, header_.dcHuffmanTables_[header_.components[1].dcHuffmanTableId], header_.acHuffmanTables_[header_.components[1].acHuffmanTableId]);
        lastDcCoefficient = m.chrominance[0].dcCoefficient;
        readBlock(m.chrominance[1], b, lastDcCoefficient, header_.dcHuffmanTables_[header_.components[2].dcHuffmanTableId], header_.acHuffmanTables_[header_.components[2].acHuffmanTableId]);
    }
}

void Jpeg::readBlock(Channel &c, BitReader &b, int lastDcCoefficient, HuffmanTable &dc, HuffmanTable &ac) {
    unsigned char symbol = getNextSymbol(b, dc);
    unsigned int coefficientLength;
    unsigned int coefficientUnsigned;
    int coefficientSigned;

    // Get DC Coefficient
    if (symbol == 0x00) c.dcCoefficient = 0;
    else {
        coefficientLength = symbol & 0x0F;
        coefficientUnsigned = b.next(coefficientLength);
        if (coefficientUnsigned < std::pow(2, coefficientLength - 1)) {
            coefficientSigned = coefficientUnsigned - std::pow(2, coefficientLength) + 1;
        } else {
            coefficientSigned = (int) coefficientUnsigned;
        }
        c.dcCoefficient = coefficientSigned;
    }

    // Get AC Coefficients
    int coefficientsRead = 0;
    while (coefficientsRead < 63) {
        symbol = getNextSymbol(b, ac);
        if (symbol == 0x00) {
            break;
        } else if (symbol == 0xF0) {
            coefficientsRead += 16;
            continue;
        } else {
            coefficientLength = symbol & 0x0F;
            unsigned int numberOfZeros = (symbol >> 4) & 0x0F;
            coefficientsRead += numberOfZeros;
            coefficientUnsigned = b.next(coefficientLength);
            // Convert to neg value if needed
            if (coefficientUnsigned < std::pow(2, coefficientLength - 1)) {
                coefficientSigned = coefficientUnsigned - std::pow(2, coefficientLength) + 1;
            } else {
                coefficientSigned = (int) coefficientUnsigned;
            }
            c.acCoefficients[coefficientsRead] = coefficientSigned;
            ++coefficientsRead;

            if (coefficientSigned != 0 && coefficientSigned != 1) {
                ++freeBits;
            }
        }
    }

}

unsigned char Jpeg::getNextSymbol(BitReader &b, HuffmanTable &ht) {
    unsigned int code = 0;
    unsigned int codeIndex;
    bool codeFound = false;

    unsigned int codeLength = 1;
    while (codeLength <= 16 && !codeFound) {
        code = code << 1;
        code = code | (b.next() & 0x01);
        unsigned int start = ht.offsets[codeLength - 1];
        unsigned char mask = static_cast<unsigned char>(std::pow(2, codeLength) - 1);
        for (unsigned int i = start; i < ht.offsets[codeLength]; ++i) {
            if ((code & mask) == (ht.codes[i] & mask)) {
                codeFound = true;
                codeIndex = i;
                break;
            }
        }
        if (!codeFound) ++codeLength;
    }

    return ht.symbols[codeIndex];
}

void prepareFileForHiding(std::vector<unsigned char> &fileData, std::string fileName) {
    // Reduce fileName to only its name and not any path details, append it to the file data after a slash separator
    std::size_t found = fileName.find_last_of("/\\");
    fileName = fileName.substr(found + 1);

    // put filename at the end
    // pad it on the left with one / character

    fileData.push_back('/');
    for (unsigned char c : fileName) {
        fileData.push_back(c);
    }

    // Add the size of fileData as a 4 byte unsigned int to the beginning of the fileData array

    unsigned int fileDataSize = fileData.size();
    unsigned char byte;
    for (int i = 0; i < 4; ++i) {
        byte = fileDataSize >> (8 * i);
        fileData.insert(fileData.begin(), byte);
    }
}

std::string separateFileNameFromFileData(std::vector<unsigned char> &fileData) {
    std::string fileName = "";
    int separatorPos = 0;
    for (int i = fileData.size() - 1; i >= 0; --i) {
        if (fileData[i] == '/') {
            separatorPos = i;
            break;
        }
    }
    if (separatorPos == 0) throw std::runtime_error("Filename separator could not be found");
    for (int i = separatorPos + 1; i < fileData.size(); ++i) {
        fileName.append(std::string(1, fileData[i]));
    }
    fileData.erase(fileData.begin() + separatorPos, fileData.end());

    return fileName;
}

int * Jpeg::getNextCoefficient() {

    if (currentMCU >= MCUArray_.size()) throw std::runtime_error("Coefficient trying to be accessed is out of bounds");

    int * c;
    if (this->currentChannelType) {
        c = &(this->MCUArray_[this->currentMCU].luminance[this->currentChannel].acCoefficients[this->currentCoefficient]);
    } else {
        c = &(this->MCUArray_[this->currentMCU].chrominance[this->currentChannel].acCoefficients[this->currentCoefficient]);
    }
    
    currentCoefficient = ++currentCoefficient % 63;

    if (currentCoefficient == 0) {
        if (currentChannelType) { // luminance
            int mod = this->header_.components->horizontalSamplingFactor * this->header_.components->verticalSamplingFactor;
            currentChannel = ++currentChannel % mod;
            if (currentChannel == 0) {
                currentChannelType = false;
            }
        } else { // chrominance
            currentChannel = ++currentChannel % 2;
            if (currentChannel == 0) {
                currentChannelType = true;
            }
        }
    }

    if (currentCoefficient == 0 && currentChannel == 0 && currentChannelType) ++currentMCU;

    return c;
}

int * Jpeg::getNextFreeCoefficient() {

    int * c = this->getNextCoefficient();
    while (*c == 0 || *c == 1) c = this->getNextCoefficient();
    return c;
}

void Jpeg::resetCurrent() {
    this->currentCoefficient = 0;
    this->currentChannel = 0;
    this->currentChannelType = true;
    this->currentMCU = 0;
}

void Jpeg::retrieveFromJpeg(std::vector<unsigned char> &secretData) {

    resetCurrent();

    int * c;
    int size = 0;
    unsigned char byte = 0x00;

    // reads first 4 bytes of data to determine length of secret data
    for (int i = 0; i < 32; ++i) {
        c = getNextFreeCoefficient();
        size = size << 1;
        size = size | (*c & 0x01);
    }

    for (int i = 1; i <= size * 8; ++i) {  // for each hidden bit
        c = getNextFreeCoefficient();
        byte = byte << 1;
        byte = byte | (*c & 0x01);

        if (i % 8 == 0) {
            secretData.push_back(byte);
            byte = 0x00;
        }
    }
}

void saveFile(std::string &fileName, const std::vector<unsigned char> &fileData) {
    std::ofstream file(fileName, std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Could not save file.");
    }

    file.write((char *) &fileData[0], fileData.size());
    file.close();
}

void createStandardHuffmanTable(HuffmanTable &ht, std::string type, std::string component) {
    if (type != "dc" && type != "ac") throw std::runtime_error("Type needs to be 'ac' or 'dc'");
    if (component != "lum" && component != "chr") throw std::runtime_error("Component needs to be 'lum' or 'chr'");

    unsigned char dcSymbols[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B};
    unsigned char acLumSymbols[] = {
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11,
        0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
        0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
        0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
        0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52,
        0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
        0x82, 0x09, 0x0A, 0x16, 0x17, 0x18,
        0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2A, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56
        , 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76
        , 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95
        , 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3
        , 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA
        , 0xD2 , 0xD3 , 0xD4 , 0xD5 , 0xD6 , 0xD7 , 0xD8 , 0xD9 , 0xDA , 0xE1 , 0xE2 , 0xE3 , 0xE4 , 0xE5 , 0xE6 , 0xE7
        , 0xE8 , 0xE9 , 0xEA , 0xF1 , 0xF2 , 0xF3 , 0xF4 , 0xF5 , 0xF6 , 0xF7 , 0xF8 , 0xF9 , 0xFA
    };

    unsigned char acChrSymbols[] = {
        0x00, 0x01
        , 0x02
        , 0x03 , 0x11
        , 0x04 , 0x05 , 0x21 , 0x31
        , 0x06 , 0x12 , 0x41 , 0x51
        , 0x07 , 0x61 , 0x71
        , 0x13 , 0x22 , 0x32 , 0x81
        , 0x08 , 0x14, 0x42, 0x91, 0xA1,
        0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72
        ,0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18
        ,0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37
        ,0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49
        ,0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63
        ,0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75
        ,0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86
        ,0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97
        ,0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8
        ,0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9
        ,0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA
        ,0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2
        ,0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3
        ,0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA
    };

    unsigned int dcLumOffsets[] = {0, 0, 1, 6, 7, 8, 9, 10, 11, 12, 12, 12, 12, 12, 12, 12, 12};
    unsigned int dcChrOffsets[] = {0, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 12, 12, 12, 12};
    unsigned int acLumOffsets[] = {0, 0, 2, 3, 6, 9, 11, 15, 18, 23, 28, 32, 36, 36, 36, 37, 162};
    unsigned int acChrOffsets[] = {0, 0, 2, 3, 5, 9, 13, 16, 20, 27, 32, 36, 40, 40, 41, 43, 162};

    if (type == "dc") {
        for (int i = 0; i < 12; ++i) ht.symbols[i] = dcSymbols[i];

        if (component == "lum") {
            for (int i = 0; i < 17; ++i) ht.offsets[i] = dcLumOffsets[i];
        } else {
            for (int i = 0; i < 17; ++i) ht.offsets[i] = dcChrOffsets[i];
        }
    } else {
        if (component == "lum") {
            for (int i = 0; i < 162; ++i) ht.symbols[i] = acLumSymbols[i];
            for (int i = 0; i < 17; ++i) ht.offsets[i] = acLumOffsets[i];
        } else {
            for (int i = 0; i < 162; ++i) ht.symbols[i] = acChrSymbols[i];
            for (int i = 0; i < 17; ++i) ht.offsets[i] = acChrOffsets[i];
        }
    }

    getHuffmanCodes(&ht);

}

void Jpeg::createNewBitstream(std::vector<unsigned char> &bitstream) {

    BitWriter bw;
    for (int i = 0; i < MCUArray_.size(); ++i) addMCUToBitstream(MCUArray_[i], bw);
    bw.copy(bitstream, true);
}

void Header::convertSymbolToCode(unsigned char symbol, bool ac, bool chr, unsigned int &code, int &length) {
    HuffmanTable * ht;
    if (ac) ht = &acHuffmanTables_[chr];
    else ht = &dcHuffmanTables_[chr];

    for (unsigned int offset = 0; offset < 16; ++offset) {
        for (int codeIndex = ht->offsets[offset]; codeIndex < ht->offsets[offset + 1]; ++codeIndex) {
            if (ht->symbols[codeIndex] == symbol) {
                length = offset + 1;
                code = ht->codes[codeIndex];
                return;
            }
        }
    }
    std::string message = "";
    std::stringstream ss(message);
    ss << "Code for " << std::hex << (int) symbol << " cannot be found in Huffman table.";
    throw std::runtime_error(ss.str());
}

int getMinimumBinaryLength(int number) {
    if (number == 0) return 0;
    if (number < 0) number = number * - 1;

    int length = 0;
    while (number) {
        number = number >> 1;
        ++length;
    }

    return length;
}

void Jpeg::addBlockToBitstream(Channel &channel, BitWriter &bw, bool chr) {
    int coefficient;
    unsigned int zeroCount = 0;
    unsigned int code;
    int codeLength;
    unsigned int coefficientLength;
    unsigned char symbol;

    // Add the dc coefficient to bitstream

    // add the code
    coefficient = channel.dcCoefficient;
    coefficientLength = getMinimumBinaryLength(coefficient);
    symbol = 0x00 | coefficientLength;
    header_.convertSymbolToCode(symbol, false, chr, code, codeLength);
    bw.write(code, codeLength);

    // then add the coefficient
    if (coefficient < 0) coefficient = coefficient - 1;  // Convert to one's complement
    bw.write(coefficient, coefficientLength);

    // Add all the ac coefficients to bitstream

    for (int j = 0; j < 63; ++j) {
        coefficient = channel.acCoefficients[j];

        if (j == 62 && coefficient == 0) {
            // Add the End Of Block symbol
            header_.convertSymbolToCode(0x00, true, chr, code, codeLength);
            bw.write(code, codeLength);
            break;
        } else if (coefficient == 0) {
            ++zeroCount;
        } else {
            
            // Add 'skip 16 zeros' symbol
            while (zeroCount >= 16) {
                header_.convertSymbolToCode(0xF0, true, chr, code, codeLength);
                bw.write(code, codeLength);
                zeroCount -= 16;
            }

            // add code with remianing number of zeros
            coefficientLength = getMinimumBinaryLength(coefficient);
            symbol = 0x00 | zeroCount;
            symbol = symbol << 4;
            symbol = symbol | coefficientLength;
            header_.convertSymbolToCode(symbol, true, chr, code, codeLength);
            bw.write(code, codeLength);

            // then add the coefficient
            if (coefficient < 0) coefficient = coefficient - 1;  // convert to ones complement
            bw.write(coefficient, coefficientLength);

            zeroCount = 0;
        }
    }
}

void Jpeg::addMCUToBitstream(MCU &m, BitWriter &bw) {
    int numberOfLum = header_.components->horizontalSamplingFactor * header_.components->verticalSamplingFactor;

    for (int i = 0; i < numberOfLum; ++i) {
        addBlockToBitstream(m.luminance[i], bw, false);
    }

    if (header_.startOfFrame_.numberOfComponents > 1) {
        addBlockToBitstream(m.chrominance[0], bw, true);
        addBlockToBitstream(m.chrominance[1], bw, true);
    }
}

Jpeg::Jpeg(std::string filename) {
    currentMCU = 0;
    currentChannel = 0;
    currentChannelType = true;
    currentCoefficient = 0;
    freeBits = 0;

    // Load the file into memory, as an array, to be analysed
    std::vector<unsigned char> fileData = loadFile(filename);

    // Read the header (metadata) of the Jpeg before the bitstream
    header_.readHeader(fileData);

    // Read the bitstream data, using info from the header to assist
    readBitstream(fileData);
}

void Jpeg::hide(std::string filename) {

    // Load the secret file into memory, as a binary file
    std::vector<unsigned char> secretFileData = loadFile(filename);

    // Prepare the fileData for writing by adding metadata needed for recovering it later
    prepareFileForHiding(secretFileData, filename);

    if (secretFileData.size() * 8 > freeBits) throw std::runtime_error("The file trying to be hidden is too large. Choose a larger JPG image, or a smaller hidden file.");

    BitReader b(secretFileData);
    int * c;
    unsigned int bit;

    for (int i = 0; i < secretFileData.size() * 8; ++i) {
        c = getNextFreeCoefficient();
        bit = b.next();
        if (bit) {
            *c = *c | 0x01;
        } else {
            *c = *c & 0xFFFFFFFE;
        }
    }

}

void Header::createHeaderBytes(std::vector<unsigned char> &header) {
    // Add Start of Image marker
    header.push_back(0xFF);
    header.push_back(0xD8);

    // Add APP0 marker
    if (app0Marker.size() > 0) addApp0Marker(header);

    // Add start of frame marker
    addStartOfFrame(header);

    // Add DHT markers
    addHuffmanTable(header, 0, 0);
    addHuffmanTable(header, 0, 1);
    addHuffmanTable(header, 1, 0);
    addHuffmanTable(header, 1, 1);

    // Add DQT markers
    addQuantizationTables(header);

    // Add SOS marker
    addStartOfScan(header);
}

void Header::addApp0Marker(std::vector<unsigned char> &header) {
    header.push_back(0xFF);
    header.push_back(0xE0);
    for (unsigned char &c : app0Marker) header.push_back(c);
}

void Header::addStartOfScan(std::vector<unsigned char> &header) {
    header.push_back(0xFF);
    header.push_back(0xDA);

    header.push_back(0x00);
    header.push_back((unsigned char) 6 + (2 * startOfFrame_.numberOfComponents));

    header.push_back(startOfFrame_.numberOfComponents);

    unsigned char c;
    for (int i = 0; i < startOfFrame_.numberOfComponents; ++i) {
        header.push_back(components[i].identifier);
        c = components[i].dcHuffmanTableId;
        c = c << 4;
        c = c | components[i].acHuffmanTableId;
        header.push_back(c);
    }

    header.push_back(0x00);
    header.push_back(0x00);
    header.push_back(0x00);

}

void Header::addHuffmanTable(std::vector<unsigned char> &header, unsigned int tableClass, unsigned int tableID) {
    header.push_back(0xFF);
    header.push_back(0xC4);

    // add length
    header.push_back(0x00);

    // for DC: 2 + 17 + 12
    // for AC: 2 + 17 + 162
    if (tableClass) header.push_back(0xB5);
    else header.push_back(0x1F);

    // 00 -> DC LUM
    // 01 -> DC CHR
    // 10 -> AC LUM
    // 11 -> AC CHR
    unsigned char c = tableClass;
    c = c << 4;
    c = c | (tableID & 0x0F);
    header.push_back(c);

    HuffmanTable * ht;
    if (tableClass) ht = &acHuffmanTables_[tableID];
    else ht = &dcHuffmanTables_[tableID];

    int numCodes = 0;
    int totalCodes = 0;
    // Number of huffman codes of length i:
    for (int i = 0; i < 16; ++i) {
        numCodes = ht->offsets[i+1] - ht->offsets[i];
        totalCodes += numCodes;
        header.push_back(numCodes & 0xFF);
    }

    // Adding symbols in order -> if tables can be changed, may not be 162 symbols
    for (int i = 0; i < totalCodes; ++i) {
        header.push_back(ht->symbols[i]);
    }

}

void Header::addStartOfFrame(std::vector<unsigned char> &header) {

    header.push_back(0xFF);
    header.push_back(0xC0);
    header.push_back(0x00);  // first byte of length
    if (startOfFrame_.numberOfComponents == 1) header.push_back(0x0B);  // second byte of length
    else header.push_back(0x11);
    header.push_back(0x08);  // data precision
    header.push_back((startOfFrame_.imageHeight >> 8) & 0xFF);  // image height
    header.push_back((startOfFrame_.imageHeight) & 0xFF);
    header.push_back((startOfFrame_.imageWidth >> 8) & 0xFF);  // image width
    header.push_back((startOfFrame_.imageWidth) & 0xFF);
    header.push_back(startOfFrame_.numberOfComponents & 0xFF);  // Number of components

    unsigned char c;
    for (unsigned int i = 0; i < startOfFrame_.numberOfComponents; ++i) {
        header.push_back(components[i].identifier & 0xFF);  // Component Identifier
        c = components[i].horizontalSamplingFactor & 0x0F;
        c = c << 4;
        c = c | (components[i].verticalSamplingFactor & 0x0F);
        header.push_back(c);  // Sampling factors
        header.push_back(components[i].quantizationTableNumber & 0xFF);  // Quantisation table ID
    }
}

void Header::addQuantizationTables(std::vector<unsigned char> &header) {
    for (unsigned char &c : quantizationTables) header.push_back(c);
}

void Jpeg::save(std::string name) {

    // Update the huffman tables in the header
    HuffmanTable standardDcLuminance;
    HuffmanTable standardAcLuminance;
    HuffmanTable standardDcChrominance;
    HuffmanTable standardAcChrominance;
    createStandardHuffmanTable(standardDcLuminance, "dc", "lum");
    createStandardHuffmanTable(standardAcLuminance, "ac", "lum");
    createStandardHuffmanTable(standardDcChrominance, "dc", "chr");
    createStandardHuffmanTable(standardAcChrominance, "ac", "chr");
    header_.dcHuffmanTables_[0] = standardDcLuminance;
    header_.dcHuffmanTables_[1] = standardDcChrominance;
    header_.acHuffmanTables_[0] = standardAcLuminance;
    header_.acHuffmanTables_[1] = standardAcChrominance;

    // Create new unsigned char vector version of header
    std::vector<unsigned char> bytes;
    header_.createHeaderBytes(bytes);

    // Create a new bitstream using the updated huffman tables and altered coefficients
    //std::vector<unsigned char> newBitstream;
    createNewBitstream(bytes);

    // add last marker bytes
    bytes.push_back(0xFF);
    bytes.push_back(0xD9);

    // save to file
    saveFile(name, bytes);
}

void Jpeg::recover() {
    std::vector<unsigned char> secretData;
    retrieveFromJpeg(secretData);
    std::string filename = separateFileNameFromFileData(secretData);
    saveFile(filename, secretData);
}