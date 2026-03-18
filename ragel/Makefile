# ragel/ — Ragel -G2 string mutation primitives for PUA-colored UTF-8
#
# Stage 0: Scaffold + color skip + visible length
#

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-implicit-fallthrough -std=c11 -g
RAGEL   = ragel
RFLAGS  = -G2 -C

SRCS_RL = color_ops.rl
SRCS_C  = color_ops.c unicode_tables.c test_harness.c
OBJS    = color_ops.o unicode_tables.o test_harness.o tables_ascii.o

.PHONY: all clean test

all: test_harness

# Ragel .rl -> .c
color_ops.c: color_ops.rl
	$(RAGEL) $(RFLAGS) -o $@ $<

test_harness: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

tables_ascii.o: ../tests/color_ops/tables_ascii.c
	$(CC) $(CFLAGS) -c -o $@ ../tests/color_ops/tables_ascii.c

test_harness.o: test_harness.c color_ops.h
color_ops.o: color_ops.c color_ops.h unicode_tables.h
unicode_tables.o: unicode_tables.c unicode_tables.h

test: test_harness
	./test_harness

clean:
	rm -f $(OBJS) color_ops.c test_harness
