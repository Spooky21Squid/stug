# Stug - A steganography tool for Jpegs

## Information about this repository

This is the repository that you are going to use **individually** for developing your project. Please use the resources provided in the module to learn about **plagiarism** and how plagiarism awareness can foster your learning.

Regarding the use of this repository, once a feature (or part of it) is developed and **working** or parts of your system are integrated and **working**, define a commit and push it to the remote repository. You may find yourself making a commit after a productive hour of work (or even after 20 minutes!), for example. Choose commit message wisely and be concise.

Please choose the structure of the contents of this repository that suits the needs of your project but do indicate in this file where the main software artefacts are located.

## Where are the main software artefacts?

This repository is organised into a flat file structure, with all necessary files being stored under the stug/ directory. The main software artefacts are:
* main.cpp - The code that controls the main flow of the program.
* jpeg.h and jpeg.cpp - The class that represents a jpg/jpeg image and contains the Jpeg class, Header class, marker structures and related public methods
* BitWriter.cpp and BitWriter.h - The class used to write coefficients to a new bitstream one bit at a time
* BitReader.cpp and BitReader.h - The class used to read codes and coefficients from the bitstream

## How do I run stug?

The executable file is located at stug/stug.exe. Executing this file with no arguments will display a helpful manual, outlining the hide and get functions and the arguments needed for each.

To reset this executable, navigate to the stug/ directory and type 'make'. Another executable called 'stug.exe' should appear.

### Hiding
To hide a file, type stug -h followed by the path to the carrier jpg image and then the path to the secret file to hide. For example: ./stug -h stug/jpg-test-images/mug.jpg stug/secrets/secretText.txt

This will create a copy of the image in the same directiory as the original that contains the secret text.

### Recovering
To recover a file, type stug -g followed by the path to the jpg image with the hidden text. For example: ./stug -g "stug/jpg-test-images/mug - Copy.jpg"

This will create an exact copy of the hidden file in the directory where the executable is run.