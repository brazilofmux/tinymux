# Makefile for the Tier 2 RV64 softcode library.
#
# Requires: riscv64-unknown-elf-gcc (or riscv64-linux-gnu-gcc)
#           rv64strip (built from mux/src/tools/rv64strip.cpp)
#
# The output softlib.rv64 is a pre-built artifact shipped in the
# distribution.  End users do not need the cross-compiler.

CC      = riscv64-unknown-elf-gcc
CFLAGS  = -march=rv64imd -mabi=lp64d -O2 -fno-builtin -ffreestanding -nostdlib \
          -ffunction-sections -fdata-sections \
          -DLBUF_SIZE=32768
INCLUDES = -isystem src/include -I../include
# -melf64lriscv pins the 64-bit RISC-V emulation.  A riscv64-only ld
# defaults to it, but a multilib riscv-none-elf ld (e.g. the xPack
# toolchain on macOS) defaults to elf32 and the link fails otherwise.
LDFLAGS = -nostdlib -Wl,--gc-sections -Wl,-melf64lriscv -T src/softlib.ld
STRIP   = ../src/tools/rv64strip

OBJS    = src/softlib.o src/color_ops.o src/unicode_tables.o

INSTALL_DIR = ../game/bin

all: softlib.rv64

install: softlib.rv64
	cp softlib.rv64 $(INSTALL_DIR)/softlib.rv64

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

# Strip ELF to .rv64 blob, and record which toolchain produced it.
#
# The build is byte-reproducible for a GIVEN compiler, not across compiler
# versions, so a hash comparison is only meaningful on a box running the
# same toolchain that produced the committed artifact.  The fleet is not
# uniform -- this box has GCC 15.2.0, the Linux boxes are a release or two
# behind, and Windows cannot cross-compile at all -- so record the builder
# here and let tests/blob/run.sh decide which check it is entitled to make.
softlib.rv64: src/softlib.elf $(STRIP)
	$(STRIP) src/softlib.elf softlib.rv64
	@printf 'toolchain: %s\n' "$$($(CC) -dumpmachine) $$($(CC) -dumpversion)" \
	    > softlib.rv64.toolchain
	@echo "  recorded toolchain: $$($(CC) -dumpmachine) $$($(CC) -dumpversion)"

clean:
	rm -f src/*.o src/softlib.elf softlib.rv64 $(STRIP)

.PHONY: all clean
