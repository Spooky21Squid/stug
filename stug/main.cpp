#include "Jpeg.h"
#include <algorithm>

#include <iostream>

int main(int argc, char* argv[]) {

    std::string usageStatement = "Usage:\n\n./stug.exe -h <image.jpg> <secret file>\n\t- To hide secret file inside of image.jpg\n\n./stug.exe -g <image.jpg>\n\t- To get and save a secret file hidden inside image.jpg\n\n";

    if (argc > 4 || argc < 3) {
        std::cout << usageStatement;
        return 1;
    }

    std::string jpegFileName = argv[2];
    std::string secretFileName;
    Jpeg j = Jpeg(jpegFileName);

    if (std::string(argv[1]) == "-h") {  // Hide
        secretFileName = argv[3];
        
        j.hide(secretFileName);
        std::string newJpegName = jpegFileName.substr(0, jpegFileName.find_last_of(".")) + " - Copy.jpg";
        j.save(newJpegName);

        std::cout << "Hiding successful\n";

    } else if (std::string(argv[1]) == "-g") {  // Get
        try {
            j.recover();
        } catch(const std::exception& e) {
            std::cout << "Could not find a hidden file in " << jpegFileName << "\n";
        }
    } else {
        std::cout << usageStatement;
        return 1;
    }
    return 0;
}