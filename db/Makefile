CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -DTINYMUX_TYPES_DEFINED
CFLAGS = -g -O2 -DSQLITE_THREADSAFE=1
SRCDIR = ../mux/src

all: test_sqlitedb test_backend

sqlite3.o: $(SRCDIR)/sqlite/sqlite3.c $(SRCDIR)/sqlite/sqlite3.h
	$(CC) $(CFLAGS) -I$(SRCDIR)/sqlite -c -o $@ $<

sqlitedb.o: $(SRCDIR)/sqlitedb.cpp $(SRCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c -o $@ $<

sqlite_backend.o: $(SRCDIR)/sqlite_backend.cpp $(SRCDIR)/sqlite_backend.h $(SRCDIR)/storage_backend.h $(SRCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c -o $@ $<

test_sqlitedb.o: test_sqlitedb.cpp $(SRCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c -o $@ $<

test_backend.o: test_backend.cpp $(SRCDIR)/storage_backend.h $(SRCDIR)/sqlite_backend.h $(SRCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c -o $@ $<

test_sqlitedb: test_sqlitedb.o sqlitedb.o sqlite3.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_backend: test_backend.o sqlite_backend.o sqlitedb.o sqlite3.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test: test_sqlitedb test_backend
	./test_sqlitedb
	@echo ""
	./test_backend

clean:
	rm -f *.o test_sqlitedb test_backend

.PHONY: all test clean
