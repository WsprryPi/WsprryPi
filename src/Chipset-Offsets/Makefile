.PHONY: test clean
test: build/chipset_offsets_test
	./build/chipset_offsets_test

build/chipset_offsets_test: tests/chipset_offsets_test.cpp src/chipset_offsets.hpp
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++17 -Wall -Wextra -pedantic -Isrc $< -o $@

clean:
	$(RM) build/chipset_offsets_test
