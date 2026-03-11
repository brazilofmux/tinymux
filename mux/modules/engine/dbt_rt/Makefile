# Makefile for RV64IMD cross-compiled test binaries
#
# Prerequisites: riscv64-unknown-elf-gcc

CC = riscv64-unknown-elf-gcc
CFLAGS = -march=rv64imd -mabi=lp64d -O2 -ffreestanding -nostdlib
LDFLAGS = -T link64.ld -lgcc

all: test_rv64.elf

crt0.o: crt0.s
	$(CC) $(CFLAGS) -c -o $@ $<

test_rv64.o: test_rv64.c
	$(CC) $(CFLAGS) -c -o $@ $<

test_rv64.elf: test_rv64.o crt0.o link64.ld
	$(CC) $(CFLAGS) $< crt0.o $(LDFLAGS) -o $@

clean:
	rm -f *.o *.elf

.PHONY: all clean
