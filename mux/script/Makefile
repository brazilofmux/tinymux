# Makefile for the 'muxscript' script-mode binary.
# Links against libmux.so for COM infrastructure.
# Loads engine.so at runtime via dlopen (through libmux COM).

CXX = g++ -std=c++17
CXXFLAGS = -g -O2 -fPIC
DEFS = -DHAVE_CONFIG_H -DWOD_REALMS -DREALITY_LVLS
INCLUDES = -I../include -I../ganl/include -I../sqlite
LIBS = -L../game/bin -lmux -ldl -Wl,-rpath,'$$ORIGIN'

TARGET = muxscript
INSTALL_DIR = ../game/bin

SRCS = mux_main.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CXX) $(DEFS) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

install: $(TARGET)
	cp -f $(TARGET) $(INSTALL_DIR)/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all install clean
