# Makefile for the Tier 2 RV64 softcode library.
#
# Requires: riscv64-unknown-elf-gcc (or riscv64-linux-gnu-gcc)
#           rv64strip (built from mux/src/tools/rv64strip.cpp)
#
# The output softlib.rv64 is a pre-built artifact shipped in the
# distribution.  End users do not need the cross-compiler.

CC      = riscv64-unknown-elf-gcc
CFLAGS  = -march=rv64imd -mabi=lp64d -O2 -fno-builtin -ffreestanding -nostdlib
STRIP   = ../src/tools/rv64strip

all: softlib.rv64

# Build rv64strip if needed.
$(STRIP): ../src/tools/rv64strip.cpp rv64blob.h
	g++ -std=c++11 -O2 -o $(STRIP) ../src/tools/rv64strip.cpp

# Cross-compile to ELF.
src/softlib.elf: src/softlib.c src/softlib.ld
	$(CC) $(CFLAGS) -T src/softlib.ld -o $@ src/softlib.c

# Strip ELF to .rv64 blob.
softlib.rv64: src/softlib.elf $(STRIP)
	$(STRIP) src/softlib.elf softlib.rv64

clean:
	rm -f src/softlib.elf softlib.rv64 $(STRIP)

.PHONY: all clean
