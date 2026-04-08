# -----------------------------------------------------------------------------
# MIT License
#
# Copyright © 2025 - 2026 Lee C. Bussy (@lbussy)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# provided to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# -----------------------------------------------------------------------------

# ─────────────────────────────────────────────────────────────────────────────
# User-configurable knobs
# ─────────────────────────────────────────────────────────────────────────────

# Debug flags (add/remove as needed)
# 	"DEBUG_WSPR" handles the primary `llog.logS(DEBUG, "string");` debugging
# 	All other is specifically guarded within the submodules and does not
#	(should not) use LCBLog.
DEBUG_DEFS := -DDEBUG_WSPR
# DEBUG_DEFS := -DDEBUG_TRANS_SYMBOL
# DEBUG_DEFS += -DDEBUG_PPMMANAGER
# DEBUG_DEFS += -DDEBUG_SIGNAL_HANDLER

# ─────────────────────────────────────────────────────────────────────────────
# Optional journald support (sd-journal.h)
# - If sd-journal.h is available, link with -lsystemd and define LIB_JOURNALD=1
# - Otherwise, define LIB_JOURNALD=0 and do not link libsystemd
# ─────────────────────────────────────────────────────────────────────────────

SYSTEMD_CFLAGS :=
SYSTEMD_LIBS   :=
HAVE_JOURNALD  := 0

# Prefer pkg-config for libsystemd, but also allow header-only detection as fallback.
ifeq ($(shell pkg-config --exists libsystemd 2>/dev/null && echo yes),yes)
	SYSTEMD_CFLAGS := $(strip $(shell pkg-config --cflags libsystemd 2>/dev/null))
	SYSTEMD_LIBS   := $(strip $(shell pkg-config --libs   libsystemd 2>/dev/null))
	HAVE_JOURNALD  := 1
else ifneq ($(wildcard /usr/include/systemd/sd-journal.h),)
	# Header exists but pkg-config missing; link with -lsystemd anyway.
	SYSTEMD_LIBS  := -lsystemd
	HAVE_JOURNALD := 1
endif

# C++ version
CXXVER := 20

# Prefix for executing tests, "sudo" if required (set SUDO= to disable)
SUDO ?= sudo

# INI File(s)
# INI_FILE := ../../../config/wsprrypi.ini
INI_FILE := /usr/local/etc/wsprrypi.ini

# WSPR Parameters
WSPR_PARAMS := AA0NT EM18 20 40m

# Test Frequency
TEST_FREQ := 730000

# Install location
# If you prefer a different location, set it on the command
# line: make PREFIX=/usr/local/bin install
PREFIX ?= /usr/local/bin

# ─────────────────────────────────────────────────────────────────────────────
# Submodules
# ─────────────────────────────────────────────────────────────────────────────

SUBMODULE_SRCDIRS :=
SUBMODULE_SRCDIRS += $(wildcard ./Mailbox/src)
SUBMODULE_SRCDIRS += $(wildcard ./INI-Handler/src)
SUBMODULE_SRCDIRS += $(wildcard ./LCBLog/src)
SUBMODULE_SRCDIRS += $(wildcard ./MonitorFile/src)
SUBMODULE_SRCDIRS += $(wildcard ./PPM-Manager/src)
SUBMODULE_SRCDIRS += $(wildcard ./Signal-Handler/src)
SUBMODULE_SRCDIRS += $(wildcard ./Singleton/src)
SUBMODULE_SRCDIRS += $(wildcard ./WSPR-Reference/src/wspr)
SUBMODULE_SRCDIRS += $(wildcard ./WSPR-Transmitter/src)

# ─────────────────────────────────────────────────────────────────────────────
# Project naming/versioning (from Git)
# ─────────────────────────────────────────────────────────────────────────────

PRJ := $(strip $(shell 	url="$$(git remote get-url origin 2>/dev/null)"; 	if [ -n "$$url" ]; then 		echo "$$url" 		| sed -E 's#.*/##; s#\.git$$##'; 	else 		echo unknown; 	fi ))
EXE_NAME := $(shell echo $(PRJ) | tr '[:upper:]' '[:lower:]')
BRH := $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null | awk '{ gsub(/[^[:alnum:]]+/, "-"); sub(/-$$/, ""); print }' || echo unknown)
RAW_TAG := $(strip $(shell git describe --tags --match "v[0-9]*" --match "[0-9]*" --abbrev=0 2>/dev/null))
BASE_VER := $(if $(RAW_TAG),$(patsubst v%,%,$(RAW_TAG)),0.0.0)

PRE := $(shell if [ "$(BRH)" != "main" ] && [ "$(BRH)" != "master" ]; then echo "-$(BRH)"; fi)
VER := $(BASE_VER)$(PRE)

COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
VER := $(shell if ! git describe --tags --exact-match >/dev/null 2>&1; then echo "$(VER)+$(COMMIT)"; else echo "$(VER)"; fi)

OUT := $(strip $(EXE_NAME))
TEST_OUT := $(strip $(EXE_NAME)_debug)
SEMANTICS_TEST_OUT := dial_frequency_semantics_test

# ─────────────────────────────────────────────────────────────────────────────
# Output directories
# ─────────────────────────────────────────────────────────────────────────────

OBJ_DIR_RELEASE := build/obj/release
OBJ_DIR_DEBUG   := build/obj/debug
DEP_DIR         := build/dep
BIN_DIR         := build/bin

# ─────────────────────────────────────────────────────────────────────────────
# Source discovery (C++ only)
# ─────────────────────────────────────────────────────────────────────────────

LOCAL_CPP_SOURCES := $(shell find . -maxdepth 1 -type f -name '*.cpp' 2>/dev/null)
SUBMODULE_CPP_SOURCES := $(shell find $(SUBMODULE_SRCDIRS) -type f -name '*.cpp' ! -name 'main.cpp' 2>/dev/null)

CPP_SOURCES := $(LOCAL_CPP_SOURCES) $(SUBMODULE_CPP_SOURCES)
REL_CPP_SOURCES := $(patsubst ../../%,%,$(CPP_SOURCES))

CPP_OBJECTS       := $(patsubst %.cpp, $(OBJ_DIR_RELEASE)/%.o,$(REL_CPP_SOURCES))
CPP_DEBUG_OBJECTS := $(patsubst %.cpp, $(OBJ_DIR_DEBUG)/%.o,$(REL_CPP_SOURCES))
MAIN_DEBUG_OBJECT := $(OBJ_DIR_DEBUG)/./main.o
SEMANTICS_TEST_SRC := tests/dial_frequency_semantics_test.cpp
SEMANTICS_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/dial_frequency_semantics_test.o
SEMANTICS_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))

# ─────────────────────────────────────────────────────────────────────────────
# Dependency includes
# ─────────────────────────────────────────────────────────────────────────────

DEPFILES := $(wildcard $(DEP_DIR)/**/*.d)
-include $(DEPFILES)

# ─────────────────────────────────────────────────────────────────────────────
# Toolchain
# ─────────────────────────────────────────────────────────────────────────────

CXX := g++

# ─────────────────────────────────────────────────────────────────────────────
# Parallel build sizing (RAM + swap + current availability)
#
# Goals:
# - Clamp when MemAvailable is low at build time (desktop load, etc.)
# - Be conservative when swap is disabled on low-memory systems
# - Guarantee JOBS >= 1 (avoid make -j0 meaning "unlimited jobs")
# - Allow override: JOBS=1 make ...
# - Optionally clamp further if a GUI is active
# - Do not override an explicit -j passed to make
# ─────────────────────────────────────────────────────────────────────────────

MEM_KB       := $(shell awk '/^MemTotal:/ {print $$2}' /proc/meminfo)
MEM_AVAIL_KB := $(shell awk '/^MemAvailable:/ {print $$2}' /proc/meminfo)
SWAP_KB      := $(shell awk '/^SwapTotal:/ {print $$2}' /proc/meminfo)

ONE_POINT_FIVE_GIB_KB   := $(shell expr 1536 \* 1024)
THREE_POINT_FIVE_GIB_KB := $(shell expr 3584 \* 1024)

# If available memory is below this threshold, force single-job builds.
# 1280 MiB is a safer default for C++ builds when a desktop is running.
MIN_MEM_AVAIL_KB ?= $(shell expr 1280 \* 1024)

NPROCS := $(shell nproc)

JOBS_COMPUTED :=

ifeq ($(shell [ $(MEM_AVAIL_KB) -lt $(MIN_MEM_AVAIL_KB) ] && echo yes),yes)
	JOBS_COMPUTED := 1
else ifeq ($(shell [ $(MEM_KB) -lt $(ONE_POINT_FIVE_GIB_KB) ] && echo yes),yes)
	JOBS_COMPUTED := 1
else ifeq ($(shell [ $(SWAP_KB) -eq 0 ] && [ $(MEM_KB) -lt $(THREE_POINT_FIVE_GIB_KB) ] && echo yes),yes)
	JOBS_COMPUTED := 1
else ifeq ($(shell [ $(MEM_KB) -lt $(THREE_POINT_FIVE_GIB_KB) ] && echo yes),yes)
	JOBS_COMPUTED := $(shell j=$$(expr $(NPROCS) / 2); [ $$j -lt 1 ] && echo 1 || echo $$j)
else
	JOBS_COMPUTED := $(NPROCS)
endif

# Allow users to override: JOBS=1 make ...
JOBS ?= $(JOBS_COMPUTED)

# Harden against accidental JOBS=0 or empty (GNU make treats -j0 as unlimited).
ifeq ($(strip $(JOBS)),)
	JOBS := 1
endif
ifeq ($(JOBS),0)
	JOBS := 1
endif

# ─────────────────────────────────────────────────────────────────────────────
# Optional: Detect an active GUI session and clamp JOBS.
# Notes:
# - This is a hint only; MemAvailable is the primary signal.
# - Set GUI_CLAMP=0 to disable this behavior.
# ─────────────────────────────────────────────────────────────────────────────

GUI_ACTIVE := $(shell 	if pgrep -x Xorg >/dev/null 2>&1; then echo yes; 	elif pgrep -x Xwayland >/dev/null 2>&1; then echo yes; 	elif pgrep -x wayfire >/dev/null 2>&1; then echo yes; 	elif pgrep -x weston >/dev/null 2>&1; then echo yes; 	elif pgrep -x gnome-shell >/dev/null 2>&1; then echo yes; 	elif pgrep -x lightdm >/dev/null 2>&1; then echo yes; 	elif pgrep -x gdm3 >/dev/null 2>&1; then echo yes; 	elif pgrep -x sddm >/dev/null 2>&1; then echo yes; 	elif pgrep -x lxdm >/dev/null 2>&1; then echo yes; 	else echo no; fi)

GUI_CLAMP ?= 1
ifeq ($(GUI_CLAMP),1)
ifeq ($(GUI_ACTIVE),yes)
	JOBS := $(shell j=$(JOBS); h=$$(expr $$j / 2); [ $$h -lt 1 ] && echo 1 || echo $$h)
endif
endif

# Apply -j only if the user did not already specify one (e.g., make -j1).
ifneq ($(filter -j%,$(MAKEFLAGS)),)
# User already set -j in MAKEFLAGS/command line. Do not override.
else
MAKEFLAGS += -j$(JOBS)
endif

# ─────────────────────────────────────────────────────────────────────────────
# libgpiod flags and version detection (build-time define)
# ─────────────────────────────────────────────────────────────────────────────

# Pull compiler and linker flags from pkg-config (compile flags go to *FLAGS,
# link flags go to LDFLAGS).
PKG_CFLAGS := $(strip $(shell pkg-config --cflags libgpiod libgpiodcxx 2>/dev/null))
PKG_LIBS   := $(strip $(shell pkg-config --libs   libgpiod libgpiodcxx 2>/dev/null))

# Determine major version from pkg-config. Prefer libgpiod, fall back to libgpiodcxx.
GPIOD_MODVER := $(strip $(shell pkg-config --modversion libgpiod 2>/dev/null))
ifeq ($(GPIOD_MODVER),)
GPIOD_MODVER := $(strip $(shell pkg-config --modversion libgpiodcxx 2>/dev/null))
endif
GPIOD_API_MAJOR := $(strip $(shell printf '%s\n' "$(GPIOD_MODVER)" | cut -d. -f1))
ifeq ($(GPIOD_API_MAJOR),)
GPIOD_API_MAJOR := 1
endif

# ─────────────────────────────────────────────────────────────────────────────
# Flags
# ─────────────────────────────────────────────────────────────────────────────

COMMON_FLAGS := -Wall -Werror -fmax-errors=10 -MMD -MP

# Base compiler flags
CXXFLAGS := -Wno-psabi -std=c++$(CXXVER) \
           $(COMMON_FLAGS) \
           $(PKG_CFLAGS) \
           $(SYSTEMD_CFLAGS) \
           -DGPIOD_API_MAJOR=$(GPIOD_API_MAJOR) \
           -DLIB_JOURNALD=$(HAVE_JOURNALD)

# Include paths
CXXFLAGS += -I$(abspath .)
CXXFLAGS += $(foreach dir,$(SUBMODULE_SRCDIRS),-I$(abspath $(dir)))
CXXFLAGS += -I$(abspath ./WSPR-Reference/include)

# Build metadata defines
CXXFLAGS += -DMAKE_TAG=\"$(VER)\" \
            -DMAKE_BRH=\"$(BRH)\" \
            -DMAKE_EXE=\"$(OUT)\" \
            -DMAKE_PRJ=\"$(PRJ)\"

# Debug/release variants
CXX_DEBUG_FLAGS   := $(CXXFLAGS) -g $(DEBUG_DEFS)
CXX_RELEASE_FLAGS := $(CXXFLAGS) -O2

# Linker flags
LDFLAGS := -lpthread -latomic \
           $(PKG_LIBS) \
           $(SYSTEMD_LIBS)

# ─────────────────────────────────────────────────────────────────────────────
# Verbosity control
# ─────────────────────────────────────────────────────────────────────────────

VERBOSE ?= 0
ifeq ($(VERBOSE),1)
	Q :=
else
	Q := @
endif

# ─────────────────────────────────────────────────────────────────────────────
# Compile rules (debug)
# ─────────────────────────────────────────────────────────────────────────────

$(OBJ_DIR_DEBUG)/%.o: %.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $*)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

define MAKE_DEBUG_SUBMOD_RULES
$(OBJ_DIR_DEBUG)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.cpp
	$(Q)mkdir -p $$(@D)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)echo "Compiling (debug) $$< into $$@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/$$*.d -c $$< -o $$@
endef
$(foreach dir,$(SUBMODULE_SRCDIRS),$(eval $(call MAKE_DEBUG_SUBMOD_RULES,$(dir))))

# Link debug binary
$(BIN_DIR)/$(TEST_OUT): $(CPP_DEBUG_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(TEST_OUT) (libgpiod major: $(GPIOD_API_MAJOR))"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(SEMANTICS_TEST_OBJ): $(SEMANTICS_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/dial_frequency_semantics_test.d -c $< -o $@

$(BIN_DIR)/$(SEMANTICS_TEST_OUT): $(SEMANTICS_TEST_OBJ) $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(SEMANTICS_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

# ─────────────────────────────────────────────────────────────────────────────
# Compile rules (release)
# ─────────────────────────────────────────────────────────────────────────────

$(OBJ_DIR_RELEASE)/%.o: %.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $*)
	$(Q)echo "Compiling (release) $< into $@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

define MAKE_RELEASE_SUBMOD_RULES
$(OBJ_DIR_RELEASE)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.cpp
	$(Q)mkdir -p $$(@D)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)echo "Compiling (release) $$< into $$@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/$$*.d -c $$< -o $$@
endef
$(foreach dir,$(SUBMODULE_SRCDIRS),$(eval $(call MAKE_RELEASE_SUBMOD_RULES,$(dir))))

# Link release binary
$(BIN_DIR)/$(OUT): $(CPP_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking release: $(OUT) (libgpiod major: $(GPIOD_API_MAJOR))"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) $^ -o $@ $(LDFLAGS)

# ─────────────────────────────────────────────────────────────────────────────
# Phony targets
# ─────────────────────────────────────────────────────────────────────────────

.PHONY: clean release debug test test-tone test-oneshot semantics-test gdb install debuginstall uninstall lint macros help
DEBUG: debug
RELEASE: release

clean:
	$(Q)echo "Cleaning up build artifacts."
	$(Q)$(SUDO) rm -rf build
	$(Q)$(SUDO) rm -rf ../executables

release: $(BIN_DIR)/$(OUT)
	$(Q)echo "Release build completed successfully."

debug: $(BIN_DIR)/$(TEST_OUT)
	$(Q)echo "Debug build completed successfully."

test: debug
	$(Q)echo "Running test with sudo privileges."
	$(Q)$(SUDO) ./$(BIN_DIR)/$(TEST_OUT) -D -i $(INI_FILE)

test-tone: debug
	$(Q)echo "Running test tone with sudo privileges."
	$(Q)$(SUDO) ./$(BIN_DIR)/$(TEST_OUT) -D --test-tone $(TEST_FREQ)

test-oneshot: debug
	$(Q)echo "Running test oneshot with sudo privileges."
	$(Q)$(SUDO) ./$(BIN_DIR)/$(TEST_OUT) -D -x 1 $(WSPR_PARAMS)

semantics-test: $(BIN_DIR)/$(SEMANTICS_TEST_OUT)
	$(Q)echo "Running dial/actual frequency semantics tests."
	$(Q)./$(BIN_DIR)/$(SEMANTICS_TEST_OUT)

gdb: debug
	$(Q)echo "Running debug session."
	$(Q)$(SUDO) gdb --args ./$(BIN_DIR)/$(TEST_OUT) -D -i $(INI_FILE)

install: release
	$(Q)echo "Installing $(OUT) to $(PREFIX)."
	$(Q)echo "$(PREFIX)/$(OUT) status before install:"
	$(Q) ls -al $(PREFIX)/$(OUT) || { $(Q)echo "Error: Daemon binary not found at $(PREFIX)/$(OUT)."; }
	$(Q)$(SUDO) systemctl stop $(OUT).service || true
	$(Q)$(SUDO) install -d $(PREFIX)
	$(Q)$(SUDO) install -m 755 $(BIN_DIR)/$(OUT) $(PREFIX)/$(OUT)
	$(Q)echo "$(PREFIX)/$(OUT) status after install:"
	$(Q)$(SUDO) ls -al $(PREFIX)/$(OUT) || { echo "Error: Daemon binary not found at $(PREFIX)/$(OUT)."; }
	$(Q)$(SUDO) systemctl enable $(OUT).service || true
	$(Q)$(SUDO) systemctl start $(OUT).service || true
	$(Q)echo "Installation complete."

debuginstall: debug
	$(Q)echo "Installing DEBUG $(OUT) to $(PREFIX)."
	$(Q)echo "$(PREFIX)/$(OUT) status before install:"
	$(Q) ls -al $(PREFIX)/$(OUT) || { $(Q)echo "Error: Daemon binary not found at $(PREFIX)/$(OUT)."; }
	$(Q)$(SUDO) systemctl stop $(OUT).service || true
	$(Q)$(SUDO) install -d $(PREFIX)
	$(Q)$(SUDO) install -m 755 $(BIN_DIR)/$(TEST_OUT) $(PREFIX)/$(OUT)
	$(Q)echo "$(PREFIX)/$(OUT) status after install:"
	$(Q)$(SUDO) ls -al $(PREFIX)/$(OUT) || { echo "Error: Daemon binary not found at $(PREFIX)/$(OUT)."; }
	$(Q)$(SUDO) systemctl enable $(OUT).service || true
	$(Q)$(SUDO) systemctl start $(OUT).service || true
	$(Q)echo "Installation complete."

uninstall:
	$(Q)echo "Removing $(PREFIX)/$(OUT)."
	$(Q)$(SUDO) systemctl stop $(OUT).service || true
	$(Q)$(SUDO) systemctl disable $(OUT).service || true
	$(Q)$(SUDO) rm -f $(PREFIX)/$(OUT)

lint:
	$(Q)command -v cppcheck >/dev/null 2>&1 || { 	  echo "Warning: cppcheck not installed."; exit 1; }
	$(Q)echo "Running cppcheck."
	$(Q)cppcheck --std=c++$(CXXVER) --enable=all --inconclusive 	      --force --inline-suppr --quiet $(CPP_SOURCES)

macros:
	$(Q)echo "Project macros."
	$(Q)$(CXX) $(CXXFLAGS) -dM -E -x c++ /dev/null 	      | grep -v '^#define _' || true

help:
	$(Q)echo "Available targets:"
	$(Q)echo "  release      Build optimized binary"
	$(Q)echo "  debug        Build debug binary"
	$(Q)echo "  test         Run tests"
	$(Q)echo "  gdb          Debug with gdb"
	$(Q)echo "  test-tone    Run the binary with a test tone"
	$(Q)echo "  test-oneshot Run the binary with one iteration"
	$(Q)echo "  install      Install the binary to $(PREFIX)"
	$(Q)echo "  debuginstall Install the debug binary to $(PREFIX)"
	$(Q)echo "  uninstall    Remove the installed binary"
	$(Q)echo "  lint         Run static analysis"
	$(Q)echo "  macros       Show macros"
	$(Q)echo "  clean        Remove build artifacts"
	$(Q)echo "  help         This message"
