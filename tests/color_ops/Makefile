# Makefile — Standalone co_* test harness
#
# Sources:
#   ../../mux/lib/color_ops.c        — Ragel-generated co_* functions
#   ../../ragel/unicode_tables.c     — Case mapping DFA tables (real OTT)
#   tables_extra.c                   — GCB/width/extpict tables (extracted)
#   co_console_width.c               — Console width function
#   test_color_ops.c                 — Test harness
#
# Build: make
# Run:   ./test_color_ops [-v] [-f <suite>] [-s <seed>]

CC       = gcc
CFLAGS   = -O2 -Wall -Wextra -std=c11 -g
INCLUDES = -I../../mux/include -I../../ragel

TARGET   = test_color_ops

# Object files
OBJS = test_color_ops.o color_ops.o unicode_tables.o tables_extra.o co_console_width.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test_color_ops.o: test_color_ops.c ../../mux/include/color_ops.h
	$(CC) $(CFLAGS) $(INCLUDES) -c test_color_ops.c

color_ops.o: ../../mux/lib/color_ops.c ../../mux/include/color_ops.h ../../mux/include/unicode_tables_c.h
	$(CC) $(CFLAGS) $(INCLUDES) -c ../../mux/lib/color_ops.c -o color_ops.o

unicode_tables.o: ../../ragel/unicode_tables.c ../../ragel/unicode_tables.h
	$(CC) $(CFLAGS) -I../../ragel -c ../../ragel/unicode_tables.c -o unicode_tables.o

# Extract GCB, ExtPict, and width tables from rv64 source (lines 19-720).
# These arrays are not in ragel/unicode_tables.c.
tables_extra.c: ../../mux/rv64/src/unicode_tables.c
	@echo "/* Auto-extracted GCB/width/extpict tables from rv64/src/unicode_tables.c */" > $@
	@echo "#include <stddef.h>" >> $@
	@echo "#include \"unicode_tables_c.h\"" >> $@
	sed -n '19,720p' ../../mux/rv64/src/unicode_tables.c >> $@

tables_extra.o: tables_extra.c ../../mux/include/unicode_tables_c.h
	$(CC) $(CFLAGS) -I../../mux/include -c tables_extra.c

co_console_width.o: co_console_width.c
	$(CC) $(CFLAGS) -c co_console_width.c

test: $(TARGET)
	./$(TARGET)

verbose: $(TARGET)
	./$(TARGET) -v

clean:
	rm -f $(OBJS) $(TARGET) tables_extra.c

.PHONY: all test verbose clean
