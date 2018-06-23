all: histogram fileinfo data2bmp

histogram: Makefile histogram.cpp
	${CXX} ${CXXFLAGS} -std=c++14 histogram.cpp -o histogram -lraw -g
scanline: Makefile scanline.cpp
	${CXX} ${CXXFLAGS} -std=c++14 scanline.cpp -o scanline -lraw -g
fileinfo: Makefile fileinfo.cpp
	${CXX} ${CXXFLAGS} -std=c++14 fileinfo.cpp -o fileinfo -lraw -g
data2bmp: Makefile data2bmp.cpp
	${CXX} ${CXXFLAGS} -std=c++14 data2bmp.cpp -o data2bmp -lraw -g
