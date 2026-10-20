# Makefile for building most C++ or C++ and C projects

# C++ Version
CXXVER := 20
# Debug tag
DEBUG := -DDEBUG_SIGNAL_HANDLER

PRJ := Signal-Handler
EXE_NAME := signal-handler

# Output Items
OUT := $(EXE_NAME)					# Normal release binary
TEST_OUT :=	$(EXE_NAME)_test		# Debug/test binary
# Strip whitespace from comments
OUT := $(strip $(OUT))
TEST_OUT := $(strip $(TEST_OUT))

# Output directories
OBJ_DIR_RELEASE = build/obj/release
OBJ_DIR_DEBUG   = build/obj/debug
DEP_DIR         = build/dep
BIN_DIR		 	= build/bin

# Keep the reusable implementation, demonstration, and bounded test separate.
C_SOURCES :=
LIB_SOURCES := signal_handler.cpp signal_wait.cpp
DEMO_SOURCES := main.cpp
TEST_SOURCES := test.cpp
CPP_SOURCES := $(LIB_SOURCES) $(DEMO_SOURCES) $(TEST_SOURCES)

# Collect object files
C_OBJECTS   := $(patsubst %.c,$(OBJ_DIR_RELEASE)/%.o,$(C_SOURCES))
CPP_OBJECTS := $(patsubst %.cpp,$(OBJ_DIR_RELEASE)/%.o,$(LIB_SOURCES) $(DEMO_SOURCES))

# Linker Flags
LDFLAGS := -lpthread  -latomic
# Get packages for linker from PKG_CONFIG_PATH
# LDFLAGS += $(shell pkg-config --cflags --libs libgpiod)

# Collect dependency files
DEPFILES := $(wildcard $(DEP_DIR)/**/*.d)
# Include dependencies if they exist
-include $(DEPFILES)

# Compiler Executables
CXX = g++
CC = gcc

# Allow threading using all available processors unless the caller already
# supplied or inherited a parallel job setting.
NPROCS := $(shell n=$$(getconf _NPROCESSORS_ONLN 2>/dev/null); if [ "$$n" -ge 1 ] 2>/dev/null; then echo "$$n"; else echo 1; fi)
ifeq ($(filter -j%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(NPROCS)
endif

# Compiler Flags
#
# Common flags for both C and C++ compilers
COMMON_FLAGS := -Wall -Werror -fmax-errors=10 -MMD -MP

# C Flags
CFLAGS := $(COMMON_FLAGS)
# C Debug Flags
C_DEBUG_FLAGS := $(CFLAGS) -g -DDEBUG_PPMMANAGER	# Debug flags
# C Release Flags
C_RELEASE_FLAGS := $(CFLAGS) -O2					# Release optimized

# C++ Flags
CXXFLAGS := -Wno-psabi -lstdc++fs -std=c++$(CXXVER)
CXXFLAGS += $(COMMON_FLAGS) $(COMM_CXX_FLAGS)
# C++ Debug Flags
CXX_DEBUG_FLAGS := $(CXXFLAGS) -g $(DEBUG)	# Debug flags
# C++ Release Flags
CXX_RELEASE_FLAGS := $(CXXFLAGS) -O2		# Release optimized

# Include paths for libraries
# CXXFLAGS += -I$(abspath ./{folder}/src)

# Define additional macros for the compiler
# CXXFLAGS += -DXXX

# Strip whitespaces
C_DEBUG_FLAGS := $(strip $(C_DEBUG_FLAGS))
C_RELEASE_FLAGS := $(strip $(C_RELEASE_FLAGS))
CXX_DEBUG_FLAGS := $(strip $(CXX_DEBUG_FLAGS))
CXX_RELEASE_FLAGS := $(strip $(CXX_RELEASE_FLAGS))

# Enable verbose output if VERBOSE=1 is specified during the build
VERBOSE ?= 0
ifeq ($(VERBOSE), 1)
	Q :=
else
	Q := @
endif

##
# Compile and Link
##

# Compile C source files for debug
$(OBJ_DIR_DEBUG)/%.o: %.c
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $<)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CC) $(C_DEBUG_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Compile C++ source files for debug
$(OBJ_DIR_DEBUG)/%.o: %.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $<)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Link the debug binary
build/bin/$(TEST_OUT): $(patsubst %.cpp,$(OBJ_DIR_DEBUG)/%.o,$(LIB_SOURCES) $(TEST_SOURCES))
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug binary: $(TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o build/bin/$(TEST_OUT) $(LDFLAGS)

# Compile C++ source files (release)
$(OBJ_DIR_RELEASE)/%.o: %.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $<)
	$(Q)echo "Compiling (release) $< into $@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Compile C source files (release)
$(OBJ_DIR_RELEASE)/%.o: %.c
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $<)
	$(Q)echo "Compiling (release) $< into $@"
	$(Q)$(CC) $(C_RELEASE_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Link the final binary (release)
build/bin/$(OUT): $(C_OBJECTS) $(CPP_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking release binary: $(OUT)"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) $^ -o $@ $(LDFLAGS)

##
# Make Targets
##

# Force the fefault to be release (order matters in targets otherwise)
.DEFAULT_GOAL := release

# Clean dirs
.PHONY: clean
clean:
	$(Q)echo "Cleaning up build artifacts."
	$(Q)rm -rf build

# Release target
.PHONY: release
release: build/bin/$(OUT)
	$(Q)echo "Release build completed successfully."

# Debug target
.PHONY: debug
debug: build/bin/$(TEST_OUT)
	$(Q)echo "Debug build completed successfully."

# Test target
.PHONY: test
test: debug
	$(Q)echo "Running bounded unprivileged signal-handler test."
	$(Q)./build/bin/$(TEST_OUT)

.PHONY: portability-test
portability-test: test
	$(Q)python3 signal_wait_source_test.py

.PHONY: live-test
live-test: release
	$(Q)test "$(SIGNAL_HANDLER_LIVE_TEST)" = "YES" || { \
		echo "Refusing interactive demo; set SIGNAL_HANDLER_LIVE_TEST=YES."; \
		exit 1; \
	}
	$(Q)./build/bin/$(OUT)

# Show only user-defined macros
.PHONY: macros
macros:
	$(Q)echo "Defined Project Macros:"
	$(Q)$(CXX) $(CXXFLAGS) -dM -E -x c++ /dev/null | grep -v '^#define _' || true

# Lint target
.PHONY: lint
lint:
	$(Q)if ! command -v cppcheck >/dev/null 2>&1; then \
	    echo "Warning: cppcheck is not installed. Please install it to run static analysis."; \
	    exit 1; \
	fi
	$(Q)echo "Running static analysis with cppcheck."
	$(Q)cppcheck --platform=unix32 --std=c++$(CXXVER) --enable=all --inconclusive --force --inline-suppr --quiet $(CPP_SOURCES)

# Display available make targets
.PHONY: help
help:
	$(Q)echo "Available targets:"
	$(Q)echo "  all          Build the project (default: release)."
	$(Q)echo "  clean        Remove build artifacts."
	$(Q)echo "  test         Run the bounded unprivileged callback/shutdown test."
	$(Q)echo "  portability-test Run lifecycle and compile-time signal-wait tests."
	$(Q)echo "  live-test    Run the interactive demo (requires SIGNAL_HANDLER_LIVE_TEST=YES)."
	$(Q)echo "  lint         Run static analysis."
	$(Q)echo "  macros       Show defined project macros."
	$(Q)echo "  debug        Build with debugging symbols."
	$(Q)echo "  release      Build optimized for production."
	$(Q)echo "  help         Show this help message."
