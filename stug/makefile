CXX = g++
CXXFLAGS = -O0 -g3 -std=c++14

All: all
all: main

main: Jpeg.o BitReader.o BitWriter.o
	$(CXX) $(CXXFLAGS) main.cpp Jpeg.o BitReader.o BitWriter.o -o stug

Jpeg.o: Jpeg.cpp Jpeg.h
	$(CXX) $(CXXFLAGS) -c Jpeg.cpp

BitReader.o: BitReader.cpp BitReader.h
	$(CXX) $(CXXFLAGS) -c BitReader.cpp

BitWriter.o: BitWriter.cpp BitWriter.h
	$(CXX) $(CXXFLAGS) -c BitWriter.cpp

deepclean:
	rm -f *~ *.o main *.exe *.stackdump

clean:
	rm -f *~ *.o *.stackdump