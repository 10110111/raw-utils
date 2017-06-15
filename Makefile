histogram: Makefile histogram.cpp
	${CXX} ${CXXFLAGS} -std=c++14 histogram.cpp -o histogram -lraw -g
