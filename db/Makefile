CXX = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra
LDFLAGS = -lsqlite3

all: test_sqlitedb

sqlitedb.o: sqlitedb.cpp sqlitedb.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test_sqlitedb.o: test_sqlitedb.cpp sqlitedb.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test_sqlitedb: test_sqlitedb.o sqlitedb.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: test_sqlitedb
	./test_sqlitedb

clean:
	rm -f *.o test_sqlitedb

.PHONY: all test clean
