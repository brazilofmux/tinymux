CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -DTINYMUX_TYPES_DEFINED
CFLAGS = -g -O2 -DSQLITE_THREADSAFE=1
INCDIR = ../mux/include
ENGDIR = ../mux/modules/engine
SQLITEDIR = ../mux/sqlite

all: test_sqlitedb test_backend

sqlite3.o: $(SQLITEDIR)/sqlite3.c $(SQLITEDIR)/sqlite3.h
	$(CC) $(CFLAGS) -I$(SQLITEDIR) -c -o $@ $<

sqlitedb.o: $(ENGDIR)/sqlitedb.cpp $(INCDIR)/sqlitedb.h $(SQLITEDIR)/sqlite3.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -c -o $@ $<

sqlite_backend.o: $(ENGDIR)/sqlite_backend.cpp $(INCDIR)/sqlite_backend.h $(INCDIR)/storage_backend.h $(INCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -c -o $@ $<

test_sqlitedb.o: test_sqlitedb.cpp $(INCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -c -o $@ $<

test_backend.o: test_backend.cpp $(INCDIR)/storage_backend.h $(INCDIR)/sqlite_backend.h $(INCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -c -o $@ $<

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
