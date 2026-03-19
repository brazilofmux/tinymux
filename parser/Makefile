CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -O2

HEADERS = mux_parse.h

all: tokenize parse eval

tokenize: tokenize.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $<

parse: parse.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $<

eval: eval.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f tokenize parse eval

.PHONY: all clean
