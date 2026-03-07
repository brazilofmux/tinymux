CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -O2

all: tokenize

tokenize: tokenize.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f tokenize

.PHONY: all clean
