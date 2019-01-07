all: histogram fileinfo data2bmp

histogram: Makefile histogram.cpp
	${CXX} -std=c++14 histogram.cpp -o histogram -lraw -g -O3 -march=native ${CXXFLAGS}
scanline: Makefile scanline.cpp
	${CXX} -std=c++14 scanline.cpp -o scanline -lraw -g -O3 -march=native ${CXXFLAGS}
fileinfo: Makefile fileinfo.cpp
	${CXX} -std=c++14 fileinfo.cpp -o fileinfo -lraw -g -O3 -march=native ${CXXFLAGS}
data2bmp: Makefile data2bmp.cpp
	${CXX} -std=c++14 data2bmp.cpp -o data2bmp -lraw -g -O3 -march=native ${CXXFLAGS}
average: Makefile average.cpp
	${CXX} -std=c++14 average.cpp -o average -lraw -g -O3 -march=native ${CXXFLAGS}
