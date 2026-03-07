CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -O2

all: tokenize parse eval

tokenize: tokenize.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

parse: parse.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

eval: eval.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f tokenize parse eval

.PHONY: all clean
