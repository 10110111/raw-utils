all: histogram fileinfo data2bmp scanline average

histogram: Makefile histogram.cpp
	${CXX} -std=c++14 histogram.cpp -o histogram -lraw -g -O3 -march=native ${CXXFLAGS} ${LDFLAGS}
scanline: Makefile scanline.cpp
	${CXX} -std=c++14 scanline.cpp -o scanline -lraw -g -O3 -march=native ${CXXFLAGS} ${LDFLAGS}
fileinfo: Makefile fileinfo.cpp
	${CXX} -std=c++14 fileinfo.cpp -o fileinfo -lraw -g -O3 -march=native ${CXXFLAGS} ${LDFLAGS}
data2bmp: Makefile data2bmp.cpp cmdline-show-help.cpp cmdline-show-help.hpp
	${CXX} -std=c++17 data2bmp.cpp cmdline-show-help.cpp -o data2bmp -lraw -ltiff -g -O3 -DNDEBUG -march=native ${CXXFLAGS} ${LDFLAGS}
average: Makefile average.cpp
	${CXX} -std=c++14 average.cpp -o average -lraw -g -O3 -march=native ${CXXFLAGS} ${LDFLAGS}
