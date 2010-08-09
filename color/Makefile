CXXFLAGS = -O9 -fomit-frame-pointer
all: kdtree

kdtree: kdtree.o
	$(CXX) $(CXXFLAGS) -o kdtree kdtree.o

kdtree.o: kdtree.cpp
	$(CXX) $(CXXFLAGS) -c kdtree.cpp

clean:
	rm *.o
