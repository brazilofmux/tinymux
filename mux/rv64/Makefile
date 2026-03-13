# Makefile for the Tier 2 RV64 softcode library.
#
# Requires: riscv64-unknown-elf-gcc (or riscv64-linux-gnu-gcc)
#           rv64strip (built from mux/src/tools/rv64strip.cpp)
#
# The output softlib.rv64 is a pre-built artifact shipped in the
# distribution.  End users do not need the cross-compiler.

CC      = riscv64-unknown-elf-gcc
CFLAGS  = -march=rv64imd -mabi=lp64d -O2 -fno-builtin -ffreestanding -nostdlib \
          -ffunction-sections -fdata-sections
INCLUDES = -isystem src/include -I../include
LDFLAGS = -nostdlib -Wl,--gc-sections -T src/softlib.ld
STRIP   = ../src/tools/rv64strip

OBJS    = src/softlib.o src/color_ops.o src/unicode_tables.o

all: softlib.rv64

# Build rv64strip if needed.
$(STRIP): ../src/tools/rv64strip.cpp rv64blob.h
	g++ -std=c++11 -O2 -o $(STRIP) ../src/tools/rv64strip.cpp

# Compile individual objects.
src/softlib.o: src/softlib.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

src/color_ops.o: ../lib/color_ops.c ../include/color_ops.h ../include/unicode_tables_c.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

src/unicode_tables.o: src/unicode_tables.c ../include/unicode_tables_c.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# Link all objects into ELF.
src/softlib.elf: $(OBJS) src/softlib.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

# Strip ELF to .rv64 blob.
softlib.rv64: src/softlib.elf $(STRIP)
	$(STRIP) src/softlib.elf softlib.rv64

clean:
	rm -f src/*.o src/softlib.elf softlib.rv64 $(STRIP)

.PHONY: all clean
