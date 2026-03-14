# Minimal Makefile for the 'mux' script-mode binary.
# Links against libmux.so for COM infrastructure.
# Loads engine.so at runtime via dlopen (through libmux COM).

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -fPIC
INCLUDES = -I../include
LIBS = -L../game/bin -lmux -ldl -Wl,-rpath,'$$ORIGIN'

TARGET = mux
INSTALL_DIR = ../game/bin

SRCS = mux_main.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

install: $(TARGET)
	cp -f $(TARGET) $(INSTALL_DIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all install clean
