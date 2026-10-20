# -----------------------------------------------------------------------------
# @file Makefile
# @brief Build system for the monitorfile test executable.
#
# This Makefile automates the process of building and testing monitorfile.
#
# @targets
# - `test`: Builds the test executable `monitorfile_test` with debug flags.
# - `lint`: Runs cppcheck for static code analysis on the source files.
# - `clean`: Cleans up build artifacts.
# - `help`: Displays available make targets and their descriptions.
# -----------------------------------------------------------------------------

# Target names
NAME        = singleton
TEST_NAME   = $(NAME)_test
DEMO_NAME   = $(NAME)_demo
DEMO_DEP    = $(DEMO_NAME).d

# Compiler settings
CXX         = g++
CXXFLAGS    += -Wall -Werror
CXXFLAGS    += -Wno-psabi -std=c++20 -MMD -MP
CXXFLAGS    += -g  # Enable debugging symbols
CXXFLAGS    += -pthread  # Required for multi-threaded code

# Test and retained demonstration sources
TEST_SRC    = test.cpp
DEMO_SRC    = main.cpp
TEST_OBJ    = $(TEST_SRC:.cpp=.o)
TEST_DEP    = $(TEST_OBJ:.o=.d)

# Define RM if not already set
RM ?= rm -f

# Ensure parallel compilation
MAKEFLAGS += -j$(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

# Build the test executable (Only compiles main.cpp)
test: $(TEST_NAME)
	@echo "Running $(TEST_NAME) with an available loopback UDP port."
	./$(TEST_NAME)

demo: $(DEMO_NAME)
	@test "$(SINGLETON_DEMO)" = "YES" || { \
		echo "Refusing legacy demonstration; set SINGLETON_DEMO=YES."; \
		exit 1; \
	}
	./$(DEMO_NAME)

# Compile test source files into object files
$(TEST_NAME): $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $(TEST_OBJ) -o $(TEST_NAME)

# Object file compilation
test.o: test.cpp singleton.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(DEMO_NAME): $(DEMO_SRC) singleton.hpp
	$(CXX) $(CXXFLAGS) $(DEMO_SRC) -o $(DEMO_NAME)

# Clean up build artifacts
clean:
	-$(RM) $(TEST_OBJ) $(TEST_DEP) $(TEST_NAME) $(DEMO_NAME) $(DEMO_DEP)
	-rm -rf $(TEST_NAME).dSYM $(DEMO_NAME).dSYM

# Include dependency files
-include $(TEST_DEP)

# Lint target: performs static code analysis with cppcheck
lint:
	@command -v cppcheck >/dev/null 2>&1 || { \
	    echo "Error: cppcheck not found. Please install it using:"; \
	    echo "  sudo apt install cppcheck   # Debian/Ubuntu"; \
	    echo "  sudo dnf install cppcheck   # Fedora"; \
	    echo "  brew install cppcheck       # macOS (Homebrew)"; \
	    echo "  choco install cppcheck      # Windows (Chocolatey)"; \
	    exit 1; \
	}
	cppcheck --enable=all --inconclusive --max-configs=100 --std=c++20 --language=c++ $(TEST_SRC) $(DEMO_SRC) --suppress=missingIncludeSystem

# Help target: displays available make targets and their descriptions
help:
	@echo "Available Make targets:"
	@echo "  test       - Build the test executable with debug flags."
	@echo "  demo       - Run the legacy demonstration (requires SINGLETON_DEMO=YES)."
	@echo "  lint       - Run cppcheck for static code analysis."
	@echo "  clean      - Clean up build artifacts."
	@echo "  help       - Display this help message."

.PHONY: test demo lint clean help
