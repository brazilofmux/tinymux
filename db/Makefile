CXX = g++
CXXFLAGS = -std=c++17 -g -O2 -Wall -Wextra -DTINYMUX_TYPES_DEFINED
SRCDIR = ../mux/src
LDFLAGS = -lsqlite3

all: test_sqlitedb test_backend

sqlitedb.o: $(SRCDIR)/sqlitedb.cpp $(SRCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c -o $@ $<

sqlite_backend.o: $(SRCDIR)/sqlite_backend.cpp $(SRCDIR)/sqlite_backend.h $(SRCDIR)/storage_backend.h $(SRCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c -o $@ $<

test_sqlitedb.o: test_sqlitedb.cpp $(SRCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c -o $@ $<

test_backend.o: test_backend.cpp $(SRCDIR)/storage_backend.h $(SRCDIR)/sqlite_backend.h $(SRCDIR)/sqlitedb.h
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c -o $@ $<

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
