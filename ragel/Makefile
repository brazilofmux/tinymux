# ragel/ — Ragel -G2 string mutation primitives and trigger matching
#

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-implicit-fallthrough -std=c11 -g
RAGEL   = ragel
RFLAGS  = -G2 -C

COLOR_OBJS = color_ops.o unicode_tables.o test_harness.o tables_ascii.o
TRIG_OBJS  = trigger_match.o test_trigger.o

.PHONY: all clean test test-color test-trigger

all: test_harness test_trigger_bin

# Ragel .rl -> .c
color_ops.c: color_ops.rl
	$(RAGEL) $(RFLAGS) -o $@ $<

trigger_match.c: trigger_match.rl
	$(RAGEL) $(RFLAGS) -o $@ $<

test_harness: $(COLOR_OBJS)
	$(CC) $(CFLAGS) -o $@ $(COLOR_OBJS)

test_trigger_bin: $(TRIG_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TRIG_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

tables_ascii.o: ../tests/color_ops/tables_ascii.c
	$(CC) $(CFLAGS) -c -o $@ ../tests/color_ops/tables_ascii.c

test_harness.o: test_harness.c color_ops.h
color_ops.o: color_ops.c color_ops.h unicode_tables.h
unicode_tables.o: unicode_tables.c unicode_tables.h
trigger_match.o: trigger_match.c trigger_match.h
test_trigger.o: test_trigger.c trigger_match.h

test: test-color test-trigger

test-color: test_harness
	./test_harness

test-trigger: test_trigger_bin
	./test_trigger_bin

clean:
	rm -f $(COLOR_OBJS) $(TRIG_OBJS) color_ops.c trigger_match.c \
	      test_harness test_trigger_bin
