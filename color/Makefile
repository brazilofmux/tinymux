CXXFLAGS = -O2 -Wall
LDFLAGS = -lm
all: kdtree kdtree_lab

kdtree: kdtree.o
	$(CXX) $(CXXFLAGS) -o kdtree kdtree.o $(LDFLAGS)

kdtree.o: kdtree.cpp
	$(CXX) $(CXXFLAGS) -c kdtree.cpp

kdtree_lab: kdtree_lab.o
	$(CXX) $(CXXFLAGS) -o kdtree_lab kdtree_lab.o $(LDFLAGS)

kdtree_lab.o: kdtree_lab.cpp
	$(CXX) $(CXXFLAGS) -c kdtree_lab.cpp

clean:
	rm -f *.o kdtree kdtree_lab
