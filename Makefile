CXX = g++
CXXFLAGS = -Ofast -pthread
FastHCADecoder: *.cpp *.h
	$(CXX) -o FastHCADecoder $(CXXFLAGS) *.cpp

clean:
	rm FastHCADecoder
