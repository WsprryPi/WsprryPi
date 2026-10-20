# SPDX-License-Identifier: MIT
# Standalone, hardware-free C++20 component; no parent or Pico runtime dependency.
CXX ?= c++
PYTHON ?= python3
BUILD_DIR ?= build
CXXFLAGS ?= -O2 -g
FLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror $(CXXFLAGS) -Iinclude -Isrc
SOURCES = src/codec.cpp src/frame_parser.cpp src/wire.cpp src/session.cpp src/detail/json.cpp
HEADERS = $(wildcard include/wtp/*.hpp src/detail/*.hpp)
OBJECTS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
LIBRARY = $(BUILD_DIR)/libwtp_client.a
.PHONY: all test
all: $(LIBRARY)
test: $(BUILD_DIR)/protocol_test $(BUILD_DIR)/codec_driver $(BUILD_DIR)/session_test
	$(BUILD_DIR)/protocol_test
	$(PYTHON) -B tests/vectors_test.py $(BUILD_DIR)/codec_driver
	$(BUILD_DIR)/session_test

$(BUILD_DIR)/%.o: %.cpp $(HEADERS)
	mkdir -p $(@D)
	$(CXX) $(FLAGS) -c $< -o $@

$(LIBRARY): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)

$(BUILD_DIR)/protocol_test: tests/protocol_test.cpp $(LIBRARY) $(HEADERS)
	$(CXX) $(FLAGS) $< $(LIBRARY) $(LDFLAGS) -o $@

$(BUILD_DIR)/codec_driver: tests/codec_driver.cpp $(LIBRARY) $(HEADERS)
	$(CXX) $(FLAGS) $< $(LIBRARY) $(LDFLAGS) -o $@

$(BUILD_DIR)/session_test: tests/session_test.cpp $(LIBRARY) $(HEADERS)
	$(CXX) $(FLAGS) $< $(LIBRARY) $(LDFLAGS) -o $@

# Optional test only. No reference sources enter libwtp_client.a or ordinary tests.
PICO_SOURCE ?=
REFERENCE_NAMES = endpoint job_service codec json frame_parser sha256
REFERENCE_OBJECTS = $(addprefix $(BUILD_DIR)/reference/,$(addsuffix .o,$(REFERENCE_NAMES)))
REFERENCE_HEADERS = $(wildcard $(PICO_SOURCE)/src/wtp/*.hpp)
$(REFERENCE_OBJECTS) $(BUILD_DIR)/reference_bridge.o: PROVENANCE.json
.PHONY: interop-test reference-run
interop-test:
	$(PYTHON) -B tests/verify_reference.py "$(PICO_SOURCE)"
	$(MAKE) reference-run

reference-run: $(BUILD_DIR)/reference_test
	$(BUILD_DIR)/reference_test

$(BUILD_DIR)/reference/%.o: $(PICO_SOURCE)/src/wtp/%.cpp $(REFERENCE_HEADERS)
	mkdir -p $(@D)
	$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror $(CXXFLAGS) -I"$(PICO_SOURCE)/src" -c $< -o $@

$(BUILD_DIR)/reference_bridge.o: tests/reference_bridge.cpp tests/reference_bridge.hpp $(REFERENCE_HEADERS)
	mkdir -p $(@D)
	$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror $(CXXFLAGS) -I"$(PICO_SOURCE)/src" -Itests -c $< -o $@

$(BUILD_DIR)/reference_test: tests/reference_test.cpp tests/reference_bridge.hpp $(LIBRARY) $(BUILD_DIR)/reference_bridge.o $(REFERENCE_OBJECTS)
	$(CXX) $(FLAGS) -Itests $< $(BUILD_DIR)/reference_bridge.o $(REFERENCE_OBJECTS) $(LIBRARY) $(LDFLAGS) -o $@
