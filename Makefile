all: histogram fileinfo

histogram: Makefile histogram.cpp
	${CXX} ${CXXFLAGS} -std=c++14 histogram.cpp -o histogram -lraw -g
fileinfo: Makefile fileinfo.cpp
	${CXX} ${CXXFLAGS} -std=c++14 fileinfo.cpp -o fileinfo -lraw -g
