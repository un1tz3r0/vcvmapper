CXXFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS += -std=c++17 -I../src -Wno-missing-field-initializers

mockup: mockup.cpp $(wildcard *.hpp) $(wildcard ../src/kit/*.hpp) $(wildcard ../src/pin/*.hpp ../src/marionette/*.hpp ../src/groups/*.hpp ../src/tether/*.hpp ../src/core/Settings.hpp ../src/core/Shape.hpp)
	$(CXX) $(CXXFLAGS) -o $@ $<

coretest: coretest.cpp ../src/core/Pattern.hpp ../src/core/Groups.hpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f mockup coretest *.svg

.PHONY: clean
