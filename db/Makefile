CXX = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra
LDFLAGS = -lsqlite3

all: test_sqlitedb test_backend

sqlitedb.o: sqlitedb.cpp sqlitedb.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

sqlite_backend.o: sqlite_backend.cpp sqlite_backend.h storage_backend.h sqlitedb.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test_sqlitedb.o: test_sqlitedb.cpp sqlitedb.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test_backend.o: test_backend.cpp storage_backend.h sqlite_backend.h sqlitedb.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

test_sqlitedb: test_sqlitedb.o sqlitedb.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_backend: test_backend.o sqlite_backend.o sqlitedb.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: test_sqlitedb test_backend
	./test_sqlitedb
	@echo ""
	./test_backend

clean:
	rm -f *.o test_sqlitedb test_backend

.PHONY: all test clean
