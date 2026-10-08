CXXFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS += -std=c++17 -I../src

mockup: mockup.cpp $(wildcard *.hpp) $(wildcard ../src/kit/*.hpp) $(wildcard ../src/pin/*.hpp)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f mockup *.svg

.PHONY: clean
