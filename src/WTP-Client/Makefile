# SPDX-License-Identifier: MIT
# Standalone, header-only byte-stream contract. No protocol or host dependency.
CXX ?= c++
BUILD_DIR ?= build
CXXFLAGS ?= -O2 -g
.PHONY: all test
all: $(BUILD_DIR)/transport_test
test: all
	$(BUILD_DIR)/transport_test
$(BUILD_DIR)/transport_test: tests/transport_test.cpp include/wtp/transport.hpp
	mkdir -p $(@D)
	$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror $(CXXFLAGS) -Iinclude $< $(LDFLAGS) -o $@
