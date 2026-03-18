# ragel/ — Ragel -G2 string mutation primitives and trigger matching
#

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Wno-implicit-fallthrough -std=c11 -g
RAGEL   = ragel
RFLAGS  = -G2 -C
INCLUDES = -I../mux/include -I.

COLOR_OBJS = color_ops.o unicode_tables.o test_harness.o tables_ascii.o \
	     tables_extra.o co_console_width.o co_nearest_color.o
TRIG_OBJS  = trigger_match.o test_trigger.o
COLOR_OPS_RL = ../mux/lib/color_ops.rl
ASCII_TABLES = ../mux/lib/utf8tables.cpp
EXTRA_TABLES = ../mux/rv64/src/unicode_tables.c

.PHONY: all clean test test-color test-trigger

all: test_harness test_trigger_bin

# Ragel .rl -> .c
color_ops.c: $(COLOR_OPS_RL) ../mux/include/color_ops.h ../mux/include/unicode_tables_c.h
	$(RAGEL) $(RFLAGS) -o $@ $<

trigger_match.c: trigger_match.rl
	$(RAGEL) $(RFLAGS) -o $@ $<

test_harness: $(COLOR_OBJS)
	$(CC) $(CFLAGS) -o $@ $(COLOR_OBJS)

test_trigger_bin: $(TRIG_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TRIG_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

tables_ascii.c: $(ASCII_TABLES)
	@echo "/* Auto-extracted tr_ascii DFA tables from utf8tables.cpp */" > $@
	sed -n '/^const.*tr_ascii_itt/,/^};/p' $(ASCII_TABLES) >> $@
	sed -n '/^const.*tr_ascii_sot/,/^};/p' $(ASCII_TABLES) >> $@
	sed -n '/^const.*tr_ascii_sbt/,/^};/p' $(ASCII_TABLES) >> $@

tables_extra.c: $(EXTRA_TABLES)
	@echo "/* Auto-extracted GCB/width/extpict tables from rv64/src/unicode_tables.c */" > $@
	@echo "#include <stddef.h>" >> $@
	@echo "#include \"unicode_tables_c.h\"" >> $@
	sed -n '/^const.*tr_widths_itt/,$$p' $(EXTRA_TABLES) >> $@

test_harness.o: test_harness.c color_ops.h
color_ops.o: color_ops.c color_ops.h unicode_tables.h
unicode_tables.o: unicode_tables.c unicode_tables.h
tables_extra.o: tables_extra.c ../mux/include/unicode_tables_c.h
co_console_width.o: ../tests/color_ops/co_console_width.c
co_nearest_color.o: ../tests/color_ops/co_nearest_color.c color_ops.h
trigger_match.o: trigger_match.c trigger_match.h
test_trigger.o: test_trigger.c trigger_match.h

co_console_width.o: ../tests/color_ops/co_console_width.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ ../tests/color_ops/co_console_width.c

co_nearest_color.o: ../tests/color_ops/co_nearest_color.c color_ops.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ ../tests/color_ops/co_nearest_color.c

test: test-color test-trigger

test-color: test_harness
	./test_harness

test-trigger: test_trigger_bin
	./test_trigger_bin

clean:
	rm -f $(COLOR_OBJS) $(TRIG_OBJS) color_ops.c tables_ascii.c tables_extra.c trigger_match.c \
	      test_harness test_trigger_bin
