CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -DTINYMUX_TYPES_DEFINED
CFLAGS = -g -O2 -DSQLITE_THREADSAFE=1
INCDIR = ../../mux/include
ENGDIR = ../../mux/modules/engine
SQLITEDIR = ../../mux/sqlite
MDBXDIR = ../../mux/libmdbx
LIBDIR = ../../mux/lib

all: test_sqlitedb test_backend stress_backend

sqlite3.o: $(SQLITEDIR)/sqlite3.c $(SQLITEDIR)/sqlite3.h
	$(CC) $(CFLAGS) -I$(SQLITEDIR) -c -o $@ $<

sqlitedb.o: $(ENGDIR)/sqlitedb.cpp $(INCDIR)/sqlitedb.h $(SQLITEDIR)/sqlite3.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -c -o $@ $<

sqlite_backend.o: $(ENGDIR)/sqlite_backend.cpp $(INCDIR)/sqlite_backend.h $(INCDIR)/storage_backend.h $(INCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -c -o $@ $<

mdbx.o: $(MDBXDIR)/mdbx.c $(MDBXDIR)/mdbx.h
	$(CC) $(CFLAGS) -I$(MDBXDIR) -c -o $@ $<

mdbx_backend.o: $(ENGDIR)/mdbx_backend.cpp $(INCDIR)/mdbx_backend.h $(INCDIR)/storage_backend.h $(MDBXDIR)/mdbx.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(MDBXDIR) -c -o $@ $<

test_sqlitedb.o: test_sqlitedb.cpp $(INCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -c -o $@ $<

test_backend.o: test_backend.cpp $(INCDIR)/storage_backend.h $(INCDIR)/sqlite_backend.h $(INCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -c -o $@ $<

stress_backend.o: stress_backend.cpp $(INCDIR)/storage_backend.h $(INCDIR)/sqlite_backend.h $(INCDIR)/mdbx_backend.h
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -I$(SQLITEDIR) -I$(MDBXDIR) -c -o $@ $<

stubs.o: stubs.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test_sqlitedb: test_sqlitedb.o sqlitedb.o sqlite3.o stubs.o
	$(CXX) $(CXXFLAGS) -o $@ $^

test_backend: test_backend.o sqlite_backend.o sqlitedb.o sqlite3.o stubs.o
	$(CXX) $(CXXFLAGS) -o $@ $^

stress_backend: stress_backend.o sqlite_backend.o sqlitedb.o sqlite3.o mdbx_backend.o mdbx.o stubs.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -L$(LIBDIR) -lmux -Wl,-rpath,'$$ORIGIN/$(LIBDIR)' -lpthread

test: test_sqlitedb test_backend
	./test_sqlitedb
	@echo ""
	./test_backend

clean:
	rm -f *.o test_sqlitedb test_backend stress_backend

.PHONY: all test clean
