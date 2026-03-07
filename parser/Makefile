CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -O2

all: tokenize parse

tokenize: tokenize.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

parse: parse.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f tokenize parse

.PHONY: all clean
