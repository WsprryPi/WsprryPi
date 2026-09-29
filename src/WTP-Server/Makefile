# SPDX-License-Identifier: MIT
# Standalone portable WTP/1 server core; no parent application dependency.
CXX ?= c++
BUILD_DIR ?= build
CXXFLAGS ?= -O2 -g
FLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror $(CXXFLAGS) -Iinclude
SOURCES = $(wildcard src/*.cpp)
HEADERS = $(wildcard include/wtp_server/*.hpp)
OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
LIBRARY = $(BUILD_DIR)/libwtp_server.a
.PHONY: all test
all: $(LIBRARY)
test: $(BUILD_DIR)/server_core_test
	$(BUILD_DIR)/server_core_test

$(BUILD_DIR)/%.o: src/%.cpp $(HEADERS)
	mkdir -p $(@D)
	$(CXX) $(FLAGS) -c $< -o $@

$(LIBRARY): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

$(BUILD_DIR)/server_core_test: tests/server_core_test.cpp $(LIBRARY) $(HEADERS)
	$(CXX) $(FLAGS) $< $(LIBRARY) $(LDFLAGS) -o $@
