# Makefile for building most C++ or C++ and C projects

# Debug flag
DEBUG := -DDEBUG_WSPR_TRANSMIT
# C++ Version
CXXVER := 20
comma := ,
BACKENDS ?= si5351
include si5351_transport_rules.mk
# Ordinary sibling components used by the standalone build.
COMPONENT_SRCDIRS := $(wildcard ../../WSPR-Reference/src/wspr)
COMPONENT_SRCDIRS += $(wildcard ../../Mailbox/src)
COMPONENT_SRCDIRS += $(wildcard ../../Signal-Handler/src)
COMPONENT_SRCDIRS += $(wildcard ../external)

# Stable names do not depend on surrounding Git metadata.
PRJ := WSPR-Transmitter
EXE_NAME := wspr-transmitter

# Output Items
OUT := $(EXE_NAME)					# Normal release binary
TEST_OUT :=	$(EXE_NAME)_test		# Debug/test binary
# Strip whitespace from comments
OUT := $(strip $(OUT))
TEST_OUT := $(strip $(TEST_OUT))

# Output directories
OBJ_DIR_RELEASE = build/obj/release/$(SI5351_TRANSPORT)
OBJ_DIR_DEBUG   = build/obj/debug/$(SI5351_TRANSPORT)
DEP_DIR         = build/dep/$(SI5351_TRANSPORT)
BIN_DIR		 	= build/bin

# ────────────────────────────────────────────────────────────────────────────────
# Find your project’s own .c/.cpp (this will pick up ./main.cpp or ./main.c):
LOCAL_C_SOURCES   := $(shell find . -type f -name '*.c')
LOCAL_CPP_SOURCES := $(shell find . -type f -name '*.cpp')
SI5351_TEST_SOURCE := ./si5351_backend_harness.cpp
STANDARD_FELD_TEST_SOURCE := ../tests/standard_feld_compiler_test.cpp
SI5351_PLANNER_TEST_SOURCE := ./si5351_planner_test.cpp
SI5351_TRANSITION_TEST_SOURCE := ./si5351_transition_test.cpp
SIMULATED_BACKEND_TEST_SOURCE := ./simulated_transmit_backend_test.cpp
SIMULATED_BACKEND_REALTIME_TEST_SOURCE := ./simulated_transmit_backend_realtime_test.cpp
TRANSMISSION_CONTROLLER_CONTRACT_TEST_SOURCE := ./transmission_controller_contract_test.cpp
THREAD_AFFINITY_UNAVAILABLE_TEST_SOURCE := ./thread_affinity_unavailable_test.cpp
STARTUP_QUIESCE_TEST_SOURCE := ./startup_quiesce_test.cpp
SI5351_QUALIFICATION_SOURCE := ./si5351_startup_quiesce_qualification.cpp
SI5351_QUALIFICATION_MAIN_SOURCE := ./si5351_startup_quiesce_qualification_main.cpp
SI5351_QUALIFICATION_TEST_SOURCE := ./si5351_startup_quiesce_qualification_test.cpp
# Qualification sources use .cc intentionally: normal dependency and parent
# discovery include only .cpp files.  They are compiled solely by explicit
# guarded qualification targets below.
GPIO_QUALIFICATION_SOURCE := ./gpio_startup_quiesce_qualification.cc
GPIO_QUALIFICATION_MAIN_SOURCE := ./gpio_startup_quiesce_qualification_main.cc
GPIO_QUALIFICATION_TEST_SOURCE := ./gpio_startup_quiesce_qualification_test.cc
SI5351_MIN_DRIVE_SOURCE := ./si5351_min_drive_qualification.cc
SI5351_MIN_DRIVE_MAIN_SOURCE := ./si5351_min_drive_qualification_main.cc
SI5351_MIN_DRIVE_TEST_SOURCE := ./si5351_min_drive_qualification_test.cc
LOCAL_CPP_SOURCES := $(filter-out ./si5351_transport_linux.cpp ./si5351_transport_macos.cpp,$(LOCAL_CPP_SOURCES))
LOCAL_CPP_SOURCES += $(addprefix ./,$(SI5351_TRANSPORT_SOURCE))
LOCAL_CPP_SOURCES := $(filter-out $(SI5351_TEST_SOURCE) $(STANDARD_FELD_TEST_SOURCE) $(SI5351_PLANNER_TEST_SOURCE) $(SI5351_TRANSITION_TEST_SOURCE) $(SIMULATED_BACKEND_TEST_SOURCE) $(SIMULATED_BACKEND_REALTIME_TEST_SOURCE) $(TRANSMISSION_CONTROLLER_CONTRACT_TEST_SOURCE) $(THREAD_AFFINITY_UNAVAILABLE_TEST_SOURCE) $(STARTUP_QUIESCE_TEST_SOURCE) $(SI5351_QUALIFICATION_SOURCE) $(SI5351_QUALIFICATION_MAIN_SOURCE) $(SI5351_QUALIFICATION_TEST_SOURCE),$(LOCAL_CPP_SOURCES))

# Find sources in each sibling component, explicitly skipping demo entry points.
COMPONENT_C_SOURCES   := $(shell find $(COMPONENT_SRCDIRS) -type f -name '*.c'   ! -name 'main.c')
COMPONENT_CPP_SOURCES := $(shell find $(COMPONENT_SRCDIRS) -type f -name '*.cpp' ! -name 'main.cpp')

# Combine them:
C_SOURCES   := $(LOCAL_C_SOURCES)   $(COMPONENT_C_SOURCES)
CPP_SOURCES := $(LOCAL_CPP_SOURCES) $(COMPONENT_CPP_SOURCES)

# Strip any leading ../../ from component paths so the object rules work.
REL_C_SOURCES   := $(patsubst ../../%,%,$(C_SOURCES))
REL_CPP_SOURCES := $(patsubst ../../%,%,$(CPP_SOURCES))

# Regenerate your object‐lists exactly as before:
C_OBJECTS         := $(patsubst %.c,   $(OBJ_DIR_RELEASE)/%.o,$(REL_C_SOURCES))
CPP_OBJECTS       := $(patsubst %.cpp, $(OBJ_DIR_RELEASE)/%.o,$(REL_CPP_SOURCES))
C_DEBUG_OBJECTS   := $(patsubst %.c,   $(OBJ_DIR_DEBUG)/%.o,$(REL_C_SOURCES))
CPP_DEBUG_OBJECTS := $(patsubst %.cpp, $(OBJ_DIR_DEBUG)/%.o,$(REL_CPP_SOURCES))
SI5351_TEST_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./si5351_backend_harness.o \
	$(OBJ_DIR_DEBUG)/./wspr_transmit_backend_si5351.o \
	$(OBJ_DIR_DEBUG)/./$(SI5351_TRANSPORT_SOURCE:.cpp=.o) \
	$(OBJ_DIR_DEBUG)/./si5351_device.o \
	$(OBJ_DIR_DEBUG)/./si5351_planner.o
STANDARD_FELD_TEST_OUT := build/bin/standard_feld_compiler_test
STANDARD_FELD_GPIO_DRY_RUN_TEST_OUT := build/bin/standard_feld_gpio_dry_run_test
STANDARD_FELD_RPI_EXECUTION_TEST_OUT := build/bin/standard_feld_rpi_execution_test
STANDARD_FELD_GATE_TEST_OUT := build/bin/standard_feld_execution_gate_test
STANDARD_FELD_PROGRESS_BRIDGE_TEST_OUT := build/bin/standard_feld_progress_bridge_test
SI5351_PLANNER_TEST_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./si5351_planner_test.o \
	$(OBJ_DIR_DEBUG)/./si5351_planner.o
SI5351_TRANSITION_TEST_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./si5351_transition_test.o \
	$(OBJ_DIR_DEBUG)/./transmission_controller.o \
	$(OBJ_DIR_DEBUG)/./wspr_transmit_backend_si5351.o \
	$(OBJ_DIR_DEBUG)/./$(SI5351_TRANSPORT_SOURCE:.cpp=.o) \
	$(OBJ_DIR_DEBUG)/./si5351_device.o \
	$(OBJ_DIR_DEBUG)/./si5351_planner.o
STARTUP_QUIESCE_TEST_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./startup_quiesce_test.o \
	$(OBJ_DIR_DEBUG)/./legacy_gpio_clock_model.o \
	$(OBJ_DIR_DEBUG)/./transmission_controller.o \
	$(OBJ_DIR_DEBUG)/./wspr_transmit_backend_rpi.o \
	$(OBJ_DIR_DEBUG)/./gpio_frequency_plan.o \
	$(OBJ_DIR_DEBUG)/./wspr_transmit_backend_si5351.o \
	$(OBJ_DIR_DEBUG)/./$(SI5351_TRANSPORT_SOURCE:.cpp=.o) \
	$(OBJ_DIR_DEBUG)/./si5351_device.o \
	$(OBJ_DIR_DEBUG)/./si5351_planner.o \
	$(OBJ_DIR_DEBUG)/Mailbox/src/mailbox.o \
	$(OBJ_DIR_DEBUG)/Mailbox/src/mailbox_revision.o
TRANSMISSION_CONTROLLER_CONTRACT_TEST_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./transmission_controller_contract_test.o \
	$(OBJ_DIR_DEBUG)/./transmission_controller.o
SIMULATED_BACKEND_TEST_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./simulated_transmit_backend_test.o \
	$(OBJ_DIR_DEBUG)/./simulated_transmit_backend.o
SIMULATED_BACKEND_REALTIME_TEST_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./simulated_transmit_backend_realtime_test.o \
	$(OBJ_DIR_DEBUG)/./simulated_transmit_backend.o
SI5351_QUALIFICATION_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./si5351_startup_quiesce_qualification.o \
	$(OBJ_DIR_DEBUG)/./wspr_transmit_backend_si5351.o \
	$(OBJ_DIR_DEBUG)/./$(SI5351_TRANSPORT_SOURCE:.cpp=.o) \
	$(OBJ_DIR_DEBUG)/./si5351_device.o \
	$(OBJ_DIR_DEBUG)/./si5351_planner.o
SI5351_MIN_DRIVE_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./si5351_min_drive_qualification.o \
	$(OBJ_DIR_DEBUG)/./$(SI5351_TRANSPORT_SOURCE:.cpp=.o) \
	$(OBJ_DIR_DEBUG)/./si5351_device.o \
	$(OBJ_DIR_DEBUG)/./si5351_planner.o
$(OBJ_DIR_DEBUG)/./si5351_min_drive_qualification.o \
$(OBJ_DIR_DEBUG)/./si5351_min_drive_qualification_main.o \
$(OBJ_DIR_DEBUG)/./si5351_min_drive_qualification_test.o: \
	si5351_min_drive_qualification.hpp
GPIO_QUALIFICATION_OBJECTS := \
	$(OBJ_DIR_DEBUG)/./gpio_startup_quiesce_qualification.o \
	$(OBJ_DIR_DEBUG)/./wspr_transmit_backend_rpi.o \
	$(OBJ_DIR_DEBUG)/./legacy_gpio_clock_model.o \
	$(OBJ_DIR_DEBUG)/./gpio_frequency_plan.o \
	$(OBJ_DIR_DEBUG)/Mailbox/src/mailbox.o \
	$(OBJ_DIR_DEBUG)/Mailbox/src/mailbox_revision.o
# ────────────────────────────────────────────────────────────────────────────────

# Linker Flags
LDFLAGS := $(SI5351_TRANSPORT_LIBS) -lpthread
ifeq ($(HOST_OS),Linux)
LDFLAGS += -latomic
endif
# Get packages for linker from PKG_CONFIG_PATH
# LDFLAGS += $(shell pkg-config --cflags --libs libgpiod)
# LDFLAGS += $(shell pkg-config --libs libgpiodcxx)
# Strip whitespace from LDFLAGS
LDFLAGS := $(strip $(LDFLAGS))

# Collect dependency files
DEPFILES := $(shell find $(DEP_DIR) -type f -name '*.d' 2>/dev/null)
-include $(DEPFILES)

# Compiler Executables
CXX = g++
CC = gcc

# Compiler Flags
#
# Common flags for both C and C++ compilers
CXX_VERSION_LINE := $(shell $(CXX) --version 2>/dev/null | head -n 1)
ifneq ($(findstring clang,$(CXX_VERSION_LINE)),)
COMPILER_DIAGNOSTIC_FLAGS := -ferror-limit=10
else
COMPILER_DIAGNOSTIC_FLAGS := -fmax-errors=10
endif

COMMON_FLAGS := -Wall -Werror $(COMPILER_DIAGNOSTIC_FLAGS) -MMD -MP

# C Flags
CFLAGS := $(COMMON_FLAGS)
# C Debug Flags
C_DEBUG_FLAGS := $(CFLAGS) -g $(DEBUG)	# Debug flags
# C Release Flags
C_RELEASE_FLAGS := $(CFLAGS) -O2		# Release optimized

# C++ Flags
CXXFLAGS := $(SI5351_TRANSPORT_FLAGS) $(if $(findstring clang,$(CXX_VERSION_LINE)),,-Wno-psabi) -std=c++$(CXXVER)
CXXFLAGS += $(COMMON_FLAGS) $(COMM_CXX_FLAGS)

# Include paths
CXXFLAGS += -I$(abspath .)
CXXFLAGS += -I$(abspath ../../Chipset-Offsets/src)
CXXFLAGS += -I$(abspath ../..)
CXXFLAGS += $(foreach dir,$(COMPONENT_SRCDIRS),-I$(abspath $(dir)))
CXXFLAGS += -I$(abspath ../../WSPR-Reference/src/wspr)

# C++ Debug Flags
CXX_DEBUG_FLAGS := $(CXXFLAGS) -g $(DEBUG)	# Debug flags
# C++ Release Flags
CXX_RELEASE_FLAGS := $(CXXFLAGS) -O2		# Release optimized

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
# Compile & link rules
##

# Generic debug compile (your own code)
$(OBJ_DIR_DEBUG)/%.o: %.c Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $*)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CC) $(C_DEBUG_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

$(OBJ_DIR_DEBUG)/%.o: %.cpp Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $*)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Explicitly named qualification-only .cc sources are never broadly discovered.
$(OBJ_DIR_DEBUG)/%.o: %.cc Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $*)
	$(Q)echo "Compiling qualification (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Auto-generate debug compile rules for each sibling component.
define MAKE_DEBUG_COMPONENT_RULES
$(OBJ_DIR_DEBUG)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.cpp Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $$(@D)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)echo "Compiling (debug) $$< into $$@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/$$*.d -c $$< -o $$@

$(OBJ_DIR_DEBUG)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.c Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $$(@D)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)echo "Compiling (debug) $$< into $$@"
	$(Q)$(CC)  $(C_DEBUG_FLAGS)   -MF $(DEP_DIR)/$$*.d -c $$< -o $$@
endef

$(foreach dir,$(COMPONENT_SRCDIRS),$(eval $(call MAKE_DEBUG_COMPONENT_RULES,$(dir))))

# Link debug binary
build/bin/$(TEST_OUT): $(CPP_DEBUG_OBJECTS) $(C_DEBUG_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

# Link temporary Si5351 backend harness
build/bin/si5351_backend_harness: $(SI5351_TEST_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: si5351_backend_harness"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(STANDARD_FELD_TEST_OUT): ../tests/standard_feld_compiler_test.cpp \
		execution_plan_compiler.cpp standard_feld.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(STANDARD_FELD_GPIO_DRY_RUN_TEST_OUT): \
		../tests/standard_feld_gpio_dry_run_test.cpp \
		../tests/rpi_standard_feld_dry_run.cpp \
		../tests/rpi_standard_feld_dry_run.hpp \
		execution_plan_compiler.cpp standard_feld.cpp \
		rpi_standard_feld_execution.cpp rpi_standard_feld_execution.hpp \
		execution_plan.hpp execution_plan_compiler.hpp \
		standard_feld.hpp standard_feld_asset.hpp \
		transmission_payloads.hpp transmission_request.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) \
		../tests/standard_feld_gpio_dry_run_test.cpp \
		../tests/rpi_standard_feld_dry_run.cpp \
		execution_plan_compiler.cpp standard_feld.cpp \
		rpi_standard_feld_execution.cpp \
		-o $@ $(LDFLAGS)

$(STANDARD_FELD_RPI_EXECUTION_TEST_OUT): \
		../tests/standard_feld_rpi_execution_test.cpp \
		rpi_standard_feld_execution.cpp rpi_standard_feld_execution.hpp \
		transmission_controller.cpp transmission_controller.hpp \
		transmission_backend.hpp wspr_transmit.hpp wspr_transmit_types.hpp \
		execution_plan_compiler.cpp execution_plan_compiler.hpp \
		standard_feld.cpp standard_feld.hpp standard_feld_asset.hpp \
		execution_plan.hpp transmission_payloads.hpp transmission_request.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) \
		../tests/standard_feld_rpi_execution_test.cpp \
		rpi_standard_feld_execution.cpp \
		transmission_controller.cpp \
		execution_plan_compiler.cpp standard_feld.cpp \
		-o $@ $(LDFLAGS)

$(STANDARD_FELD_GATE_TEST_OUT): ../tests/standard_feld_execution_gate_test.cpp \
		standard_feld_execution_gate.cpp standard_feld_execution_gate.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -DSTANDARD_FELD_GATE_TEST_HOOKS $< standard_feld_execution_gate.cpp -o $@ $(LDFLAGS)

$(STANDARD_FELD_PROGRESS_BRIDGE_TEST_OUT): ../tests/standard_feld_progress_bridge_test.cpp rpi_standard_feld_progress_bridge.cpp rpi_standard_feld_progress_bridge.hpp rpi_standard_feld_execution.cpp rpi_standard_feld_execution.hpp execution_plan_compiler.cpp standard_feld.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -DSTANDARD_FELD_PROGRESS_TEST_HOOKS $< rpi_standard_feld_progress_bridge.cpp rpi_standard_feld_execution.cpp execution_plan_compiler.cpp standard_feld.cpp -o $@ $(LDFLAGS)

build/bin/si5351_planner_test: $(SI5351_PLANNER_TEST_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: si5351_planner_test"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/si5351_transition_test: $(SI5351_TRANSITION_TEST_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: si5351_transition_test"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/startup_quiesce_test: $(STARTUP_QUIESCE_TEST_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: startup_quiesce_test"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/transmission_controller_contract_test: $(TRANSMISSION_CONTROLLER_CONTRACT_TEST_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/thread_affinity_unavailable_test: thread_affinity_unavailable_test.cpp thread_affinity.cpp thread_affinity.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++$(CXXVER) -Wall -Wextra -Werror \
		-DWSPRRYPI_FORCE_UNAVAILABLE_AFFINITY_FOR_TEST \
		thread_affinity_unavailable_test.cpp thread_affinity.cpp -o $@

build/bin/simulated_transmit_backend_test: $(SIMULATED_BACKEND_TEST_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/simulated_transmit_backend_realtime_test: $(SIMULATED_BACKEND_REALTIME_TEST_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/si5351_startup_quiesce_qualification: $(OBJ_DIR_DEBUG)/./si5351_startup_quiesce_qualification_main.o $(SI5351_QUALIFICATION_OBJECTS)

	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/si5351_startup_quiesce_qualification_test: $(OBJ_DIR_DEBUG)/./si5351_startup_quiesce_qualification_test.o $(SI5351_QUALIFICATION_OBJECTS)

	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/si5351_min_drive_qualification: $(OBJ_DIR_DEBUG)/./si5351_min_drive_qualification_main.o $(SI5351_MIN_DRIVE_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/si5351_min_drive_qualification_test: $(OBJ_DIR_DEBUG)/./si5351_min_drive_qualification_test.o $(SI5351_MIN_DRIVE_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/gpio_startup_quiesce_qualification: $(OBJ_DIR_DEBUG)/./gpio_startup_quiesce_qualification_main.o $(GPIO_QUALIFICATION_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking guarded live GPIO startup-quiesce qualification executable"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

build/bin/gpio_startup_quiesce_qualification_test: $(OBJ_DIR_DEBUG)/./gpio_startup_quiesce_qualification_test.o $(GPIO_QUALIFICATION_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking fake GPIO startup-quiesce qualification tests"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

# Generic release compile (your own code)
$(OBJ_DIR_RELEASE)/%.o: %.cpp Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $*)
	$(Q)echo "Compiling (release) $< into $@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

$(OBJ_DIR_RELEASE)/%.o: %.c Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $*)
	$(Q)echo "Compiling (release) $< into $@"
	$(Q)$(CC) $(C_RELEASE_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

# Auto-generate release compile rules for each sibling component.
define MAKE_RELEASE_COMPONENT_RULES
$(OBJ_DIR_RELEASE)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.cpp Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $$(@D)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)echo "Compiling (release) $$< into $$@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/$$*.d -c $$< -o $$@

$(OBJ_DIR_RELEASE)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.c Makefile si5351_transport_rules.mk
	$(Q)mkdir -p $$(@D)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)echo "Compiling (release) $$< into $$@"
	$(Q)$(CC)  $(C_RELEASE_FLAGS)   -MF $(DEP_DIR)/$$*.d -c $$< -o $$@
endef

$(foreach dir,$(COMPONENT_SRCDIRS),$(eval $(call MAKE_RELEASE_COMPONENT_RULES,$(dir))))

# Link release binary
build/bin/$(OUT): $(CPP_OBJECTS) $(C_OBJECTS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking release: $(OUT)"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) $^ -o $@ $(LDFLAGS)

##
# Phony targets
##
.PHONY: clean release debug test si5351-test standard-feld-test standard-feld-gpio-dry-run-test standard-feld-rpi-execution-test standard-feld-execution-gate-test standard-feld-progress-bridge-test si5351-planner-test si5351-transition-test simulated-backend-test simulated-backend-realtime-test transmission-controller-contract-test startup-quiesce-test si5351-startup-quiesce-qualification si5351-startup-quiesce-qualification-test si5351-min-drive-qualification si5351-min-drive-qualification-test gpio-startup-quiesce-qualification gpio-startup-quiesce-qualification-test watchdog gdb lint macros help

clean:
	$(Q)echo "Cleaning up build artifacts."
	$(Q)rm -rf build

release: build/bin/$(OUT)
	$(Q)echo "Release build completed successfully."

debug: build/bin/$(TEST_OUT)
	$(Q)echo "Debug build completed successfully."

test: legacy-tone-frequency-test simulated-backend-test simulated-backend-realtime-test transmission-controller-contract-test si5351-planner-test si5351-transition-test startup-quiesce-test
	$(Q)echo "Hardware-free component tests completed successfully."

si5351-test: build/bin/si5351_backend_harness
	$(Q)echo "Si5351 backend harness built successfully."

standard-feld-test: $(STANDARD_FELD_TEST_OUT)
	$(Q)./$(STANDARD_FELD_TEST_OUT)

standard-feld-gpio-dry-run-test: $(STANDARD_FELD_GPIO_DRY_RUN_TEST_OUT)
	$(Q)./$(STANDARD_FELD_GPIO_DRY_RUN_TEST_OUT)

standard-feld-rpi-execution-test: $(STANDARD_FELD_RPI_EXECUTION_TEST_OUT)
	$(Q)./$(STANDARD_FELD_RPI_EXECUTION_TEST_OUT)

standard-feld-execution-gate-test: $(STANDARD_FELD_GATE_TEST_OUT)
	$(Q)./$(STANDARD_FELD_GATE_TEST_OUT)

standard-feld-progress-bridge-test: $(STANDARD_FELD_PROGRESS_BRIDGE_TEST_OUT)
	$(Q)./$(STANDARD_FELD_PROGRESS_BRIDGE_TEST_OUT)

si5351-planner-test: build/bin/si5351_planner_test
	$(Q)./build/bin/si5351_planner_test

si5351-transition-test: build/bin/si5351_transition_test
	$(Q)./build/bin/si5351_transition_test

transmission-controller-contract-test: build/bin/transmission_controller_contract_test
	$(Q)./build/bin/transmission_controller_contract_test

.PHONY: thread-affinity-test
thread-affinity-test: build/bin/thread_affinity_unavailable_test
	$(Q)./build/bin/thread_affinity_unavailable_test
	$(Q)python3 thread_affinity_source_test.py

simulated-backend-test: build/bin/simulated_transmit_backend_test
	$(Q)./build/bin/simulated_transmit_backend_test

simulated-backend-realtime-test: build/bin/simulated_transmit_backend_realtime_test
	$(Q)./build/bin/simulated_transmit_backend_realtime_test

startup-quiesce-test: build/bin/startup_quiesce_test
	$(Q)./build/bin/startup_quiesce_test

.PHONY: legacy-tone-frequency-test
legacy-tone-frequency-test: build/bin/legacy_tone_frequency_test
	$(Q)./build/bin/legacy_tone_frequency_test

build/bin/legacy_tone_frequency_test: legacy_tone_frequency_test.cc gpio_frequency_plan.cpp gpio_frequency_plan.hpp legacy_gpio_clock_model.cpp legacy_gpio_clock_model.hpp execution_plan_compiler.cpp execution_plan_compiler.hpp transmission_payloads.hpp transmission_request.hpp wspr_transmit_types.hpp ../../Chipset-Offsets/src/chipset_offsets.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $(filter %.cc %.cpp,$^) -o $@ $(LDFLAGS)

si5351-startup-quiesce-qualification: build/bin/si5351_startup_quiesce_qualification
	$(Q)echo "Built live qualification executable; it is not run by this target."

si5351-startup-quiesce-qualification-test: build/bin/si5351_startup_quiesce_qualification_test
	$(Q)./build/bin/si5351_startup_quiesce_qualification_test

si5351-min-drive-qualification: build/bin/si5351_min_drive_qualification
	$(Q)echo "Built guarded one-burst live qualification executable; it is not run by this target."

si5351-min-drive-qualification-test: build/bin/si5351_min_drive_qualification_test
	$(Q)./build/bin/si5351_min_drive_qualification_test

gpio-startup-quiesce-qualification: build/bin/gpio_startup_quiesce_qualification
	$(Q)echo "Built guarded live GPIO startup-quiesce qualification executable; it is not run by this target."

gpio-startup-quiesce-qualification-test: build/bin/gpio_startup_quiesce_qualification_test
	$(Q)./build/bin/gpio_startup_quiesce_qualification_test

watchdog:
	$(Q)test "$(WSPR_TRANSMITTER_LIVE_TEST)" = "YES" || { echo "Refusing live watchdog diagnostic; set WSPR_TRANSMITTER_LIVE_TEST=YES explicitly."; exit 2; }
	$(Q)$(MAKE) debug
	$(Q)env WSPR_TX_INJECT_WD_STALL=5 ./build/bin/$(TEST_OUT) --now --oneshot

gdb:
	$(Q)test "$(WSPR_TRANSMITTER_LIVE_TEST)" = "YES" || { echo "Refusing live debugger session; set WSPR_TRANSMITTER_LIVE_TEST=YES explicitly."; exit 2; }
	$(Q)$(MAKE) debug
	$(Q)gdb --args ./build/bin/$(TEST_OUT)

lint:
	$(Q)command -v cppcheck >/dev/null 2>&1 || { \
	  echo "Warning: cppcheck not installed."; exit 1; }
	$(Q)echo "Running cppcheck…"
	$(Q)cppcheck --std=c++$(CXXVER) --enable=all --inconclusive \
	      --force --inline-suppr --quiet $(CPP_SOURCES)

macros:
	$(Q)echo "Project macros:"
	$(Q)$(CXX) $(CXXFLAGS) -dM -E -x c++ /dev/null \
	      | grep -v '^#define _' || true

help:
	$(Q)echo "Available targets:"
	$(Q)echo "  release    Build optimized binary"
	$(Q)echo "  debug      Build debug binary"
	$(Q)echo "  test       Run the hardware-free component suite"
	$(Q)echo "  si5351-test Build temporary Si5351 backend harness"
	$(Q)echo "  standard-feld-test Build and run backend-neutral Standard Feld tests"
	$(Q)echo "  si5351-planner-test Build and run non-hardware planner tests"
	$(Q)echo "  si5351-transition-test Build and run fake-I2C transition tests"
	$(Q)echo "  startup-quiesce-test Build and run non-hardware startup-quiesce tests"
	$(Q)echo "  gpio-startup-quiesce-qualification Build guarded live qualification executable only"
	$(Q)echo "  gpio-startup-quiesce-qualification-test Run fake qualification tests"
	$(Q)echo "  watchdog   Guarded live diagnostic; requires WSPR_TRANSMITTER_LIVE_TEST=YES"
	$(Q)echo "  gdb        Guarded live debugger; requires WSPR_TRANSMITTER_LIVE_TEST=YES"
	$(Q)echo "  lint       Static analysis"
	$(Q)echo "  macros     Show macros"
	$(Q)echo "  clean      Remove build artifacts"
	$(Q)echo "  help       This message"

.PHONY: linux-i2c-identity-test
linux-i2c-identity-test:
	@mkdir -p build/bin
	$(CXX) -std=c++20 -Wall -Wextra -Werror linux_i2c_identity_test.cc -o build/bin/linux_i2c_identity_test
	./build/bin/linux_i2c_identity_test

.PHONY: mcp2221a-transport-test
mcp2221a-transport-test:
	$(Q)mkdir -p build/bin
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -pthread -DMCP2221A_DEVICE_INTEGRATION mcp2221a_protocol_test.cc si5351_device.cpp $(SI5351_TRANSPORT_SOURCE) $(SI5351_TRANSPORT_LIBS) -o build/bin/mcp2221a_protocol_test
	$(Q)./build/bin/mcp2221a_protocol_test

.PHONY: si5351-startup-portable-test
si5351-startup-portable-test:
	$(Q)mkdir -p build/bin
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -DSI5351_ONLY_STARTUP_TEST startup_quiesce_test.cpp transmission_controller.cpp wspr_transmit_backend_si5351.cpp si5351_device.cpp $(SI5351_TRANSPORT_SOURCE) si5351_planner.cpp $(LDFLAGS) -o build/bin/si5351_startup_portable_test
	$(Q)./build/bin/si5351_startup_portable_test
