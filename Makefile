CXX = g++
CXXFLAGS = -Ofast -pthread
FastHCADecoder: *.cpp *.h
	$(CXX) -o clHCA $(CXXFLAGS) *.cpp

clean:
	rm clHCA
