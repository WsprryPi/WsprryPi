# Licensed under the repository-root LICENSE.md.
PRJ := MonitorFile
EXE_NAME := monitorfile

# Output Items
OUT := $(EXE_NAME)				# Normal release binary
TEST_OUT := $(EXE_NAME)_test	# Debug/test binary

# Strip whitespace from comments
OUT := $(strip $(OUT))
TEST_OUT := $(strip $(TEST_OUT))

# Output directories
OBJ_DIR_RELEASE = build/obj/release
OBJ_DIR_DEBUG   = build/obj/debug
DEP_DIR	 = build/dep
BIN_DIR	 = build/bin

# Collect source files
CPP_SOURCES := $(shell find . -name "*.cpp" ! -path "./*/main.cpp")
# Collect object files
CPP_OBJECTS := $(patsubst %.cpp,$(OBJ_DIR_RELEASE)/%.o,$(CPP_SOURCES))
# Linker Flags
LDFLAGS := -lpthread -latomic
# Collect dependency files
DEPFILES := $(wildcard $(DEP_DIR)/**/*.d)
# Include dependencies if they exist
-include $(DEPFILES)

# Compiler Executables
CXX = g++

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
# C++ Flags
COMM_CXX_FLAGS := -Wno-psabi -lstdc++fs -std=c++20
CXXFLAGS := $(COMMON_FLAGS) $(COMM_CXX_FLAGS)
# Include paths for libraries
CXXFLAGS += -I$(abspath .)
# Define additional macros for the compiler
CXXFLAGS += -DMAKE_TAG=\"$(VER)\" -DMAKE_BRH=\"$(BRH)\" -DMAKE_EXE=\"$(OUT)\" -DMAKE_PRJ=\"$(PRJ)\"
# Target flags
CXX_RELEASE_FLAGS := $(CXXFLAGS) -O2		    # Release optimized
CXX_DEBUG_FLAGS := $(CXXFLAGS) -g -DDEBUG_BUILD # Debug flags
# Strip whitespaces
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

# Compile C++ source files for debug
$(OBJ_DIR_DEBUG)/%.o: %.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $<)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Link the debug binary
build/bin/$(TEST_OUT): $(patsubst %.cpp,$(OBJ_DIR_DEBUG)/%.o,$(CPP_SOURCES))
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o build/bin/$(TEST_OUT) $(LDFLAGS)

# Compile C++ source files (release)
$(OBJ_DIR_RELEASE)/%.o: %.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $<)
	$(Q)echo "Compiling (release) $< into $@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Link the final binary (release)
build/bin/$(OUT): $(CPP_OBJECTS)
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
	$(Q)echo "Running test."
	$(Q)./build/bin/$(TEST_OUT)

# Show only user-defined macros
.PHONY: macros
macros:
	$(Q)echo "Defined Project Macros:"
	$(Q)$(CXX) $(CXXFLAGS) -dM -E -x c++ /dev/null | grep -v '^#define _' || true

# Lint target
.PHONY: lint
lint:
	$(Q)echo "Running static analysis with cppcheck."
	$(Q)cppcheck --platform=unix32 --std=c+20 --enable=all --inconclusive --force --inline-suppr --quiet $(CPP_SOURCES)

# Display available make targets
.PHONY: help
help:
	$(Q)echo "Available targets:"
	$(Q)echo "  all	  Build the project (default: release)."
	$(Q)echo "  clean	Remove build artifacts."
	$(Q)echo "  test	 Run the bounded temporary-directory file-change test."
	$(Q)echo "  lint	 Run static analysis."
	$(Q)echo "  macros       Show defined project macros."
	$(Q)echo "  debug	Build with debugging symbols."
	$(Q)echo "  release      Build optimized for production."
	$(Q)echo "  help	 Show this help message."
