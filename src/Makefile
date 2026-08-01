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
# 	All other is specifically guarded within the components and does not
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

OPENSSL_CFLAGS := $(strip $(shell pkg-config --cflags libcrypto 2>/dev/null))
OPENSSL_LIBS   := $(strip $(shell pkg-config --libs libcrypto 2>/dev/null))

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
TEST_FREQ := 14095600

# Install location
# If you prefer a different location, set it on the command
# line: make PREFIX=/usr/local/bin install
PREFIX ?= /usr/local/bin

# Compile-time transmission backend set. Each validated selection receives
# isolated objects and a retained profile executable.
DEFAULT_BACKENDS := rpi-gpio,rp1-gpclk,si5351,simulated
BACKENDS ?= $(DEFAULT_BACKENDS)
ANCILLARY_GPIO ?= 1
comma := ,
BACKEND_LIST := $(subst $(comma), ,$(BACKENDS))
BACKEND_PROFILE_KEY := $(subst $(comma),+,$(BACKENDS))+ancillary-gpio$(ANCILLARY_GPIO)

# ─────────────────────────────────────────────────────────────────────────────
# Components
# ─────────────────────────────────────────────────────────────────────────────

COMPONENT_SRCDIRS :=
COMPONENT_SRCDIRS += $(wildcard ./Mailbox/src)
COMPONENT_SRCDIRS += $(wildcard ./INI-Handler/src)
COMPONENT_SRCDIRS += $(wildcard ./LCBLog/src)
COMPONENT_SRCDIRS += $(wildcard ./MonitorFile/src)
COMPONENT_SRCDIRS += $(wildcard ./PPM-Manager/src)
COMPONENT_SRCDIRS += $(wildcard ./Signal-Handler/src)
COMPONENT_SRCDIRS += $(wildcard ./Singleton/src)
COMPONENT_SRCDIRS += $(wildcard ./WSPR-Reference/src/wspr)
COMPONENT_SRCDIRS += $(wildcard ./WSPR-Transmitter/src)

# ─────────────────────────────────────────────────────────────────────────────
# Project naming/versioning (from Git)
# ─────────────────────────────────────────────────────────────────────────────

PRJ_RAW := $(strip $(basename $(notdir $(shell git remote get-url origin 2>/dev/null))))
PRJ := $(if $(PRJ_RAW),$(PRJ_RAW),unknown)
EXE_NAME := $(shell echo $(PRJ) | tr '[:upper:]' '[:lower:]')

OUT := $(strip $(EXE_NAME))
TEST_OUT := $(strip $(EXE_NAME)_debug)
SEMANTICS_TEST_OUT := dial_frequency_semantics_test
UI_SOURCE_REGRESSION_TEST_OUT := ui_source_regression_test
GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OUT := guarded_mode_change_persistence_test
BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OUT := band_gpio_default_convergence_test
BAND_GPIO_ROTATION_TEST_OUT := band_gpio_rotation_test
MONITOR_FILE_REGRESSION_TEST_OUT := monitor_file_regression_test
GPIO_LOOPBACK_TEST_OUT := gpio_loopback_test
BAND_GPIO_LIVE_MONITOR_OUT := band_gpio_live_monitor
GPIO_LINE_RESOLVER_TEST_OUT := gpio_line_resolver_test
NON_WSPR_REPEAT_POLICY_TEST_OUT := non_wspr_repeat_policy_test
QRSS_EXECUTION_REGRESSION_TEST_OUT := qrss_execution_regression_test
WSPR_TONE_REGRESSION_TEST_OUT := wspr_tone_regression_test
GPIO_BAND_POLICY_TEST_OUT := gpio_band_policy_test
RP1_GPCLK_PLANNER_TEST_OUT := rp1_gpclk_planner_test
RP1_GPCLK_PLANNER_TEST_FLAGS = $(filter-out -fmax-errors=10,$(CXX_DEBUG_FLAGS))
RP1_GPCLK_LIFECYCLE_TEST_OUT := rp1_gpclk_lifecycle_test
RP1_GPCLK_TRANSITION_TEST_OUT := rp1_gpclk_transition_test
RP1_GPCLK_BACKEND_TEST_OUT := rp1_gpclk_backend_test
RP1_GPCLK_LINUX_PROVIDER_TEST_OUT := rp1_gpclk_linux_provider_test
RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT := rp1_gpclk_transmit_backend_test
SELECTOR_SHUTDOWN_CLEANUP_TEST_OUT := selector_shutdown_cleanup_test
STANDARD_FELD_PARENT_INTEGRATION_TEST_OUT := standard_feld_parent_integration_test
QUALIFICATION_GPIO_TEST_MODE_TEST_OUT := qualification_gpio_test_mode_test
SUPPORT_REQUEST_GUARD_TEST_OUT := support_request_guard_test
PRIVILEGED_NETWORK_POLICY_TEST_OUT := privileged_network_policy_test
BACKEND_HTTP_GUARD_TEST_OUT := backend_http_guard_test
WEBSOCKET_UPGRADE_GUARD_TEST_OUT := websocket_upgrade_guard_test
APACHE_PRIVILEGED_NETWORK_POLICY_TEST_OUT := apache_privileged_network_policy_test
PRIVILEGED_NETWORK_TRANSACTION_TEST_OUT := privileged_network_transaction_test
PRIVILEGED_NETWORK_RUNTIME_TEST_OUT := privileged_network_runtime_test
PRIVILEGED_NETWORK_RUNTIME_WIRING_TEST_OUT := privileged_network_runtime_wiring_test
PRIVILEGED_NETWORK_ADMIN_TEST_OUT := privileged_network_admin_test
SUPPORT_BUNDLE_JOB_MANAGER_TEST_OUT := support_bundle_job_manager_test
SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_OUT := support_bundle_result_validator_test
SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_OUT := support_bundle_collector_executor_test
SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_HELPER_OUT := support_bundle_collector_executor_helper
SUPPORT_BUNDLE_RUNTIME_TEST_OUT := support_bundle_runtime_test
SUPPORT_BUNDLE_HTTP_TEST_OUT := support_bundle_http_test
SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_OUT := support_bundle_web_server_wiring_test
SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_OUT := support_bundle_download_file_test
SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_OUT := support_bundle_checksum_file_test
SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_OUT := support_bundle_archive_digest_test
SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_OUT := support_bundle_download_preparation_test
SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_OUT := support_bundle_job_directory_remover_test
SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_OUT := support_bundle_startup_cleanup_test
BACKEND_PROFILE_FACTORY_TEST_OUT := backend_profile_factory_test
MACHINE_POWER_UNAVAILABLE_TEST_OUT := machine_power_control_unavailable_test
SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_OUT := support_bundle_private_artifact_test
SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OUT := support_bundle_age_roundtrip_test
SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OUT := support_bundle_intake_validation_test
SUPPORT_BUNDLE_INTAKE_STATE_TEST_OUT := support_bundle_intake_state_test
SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OUT := support_bundle_intake_retrieval_test
SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OUT := support_bundle_intake_retrieval_helper
SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OUT := support_bundle_intake_controller_test
SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OUT := support_bundle_intake_runtime_test
SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OUT := support_bundle_intake_production_test
SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS = $(filter-out -fmax-errors=10,$(CXX_DEBUG_FLAGS))
SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_LDFLAGS = $(filter-out -latomic,$(LDFLAGS))
SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS = $(filter-out -fmax-errors=10,$(CXX_DEBUG_FLAGS))
SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS = $(filter-out -latomic,$(LDFLAGS))
AGE_KEYGEN ?= $(realpath $(shell command -v age-keygen 2>/dev/null))

# ─────────────────────────────────────────────────────────────────────────────
# Output directories
# ─────────────────────────────────────────────────────────────────────────────

OBJ_DIR_RELEASE := build/obj/release/$(BACKEND_PROFILE_KEY)
OBJ_DIR_DEBUG   := build/obj/debug/$(BACKEND_PROFILE_KEY)
DEP_DIR         := build/dep
BIN_DIR         := build/bin
PROFILE_BIN_DIR := $(BIN_DIR)/profiles/$(BACKEND_PROFILE_KEY)
PROFILE_RELEASE_BIN := $(PROFILE_BIN_DIR)/$(OUT)
PROFILE_DEBUG_BIN := $(PROFILE_BIN_DIR)/$(TEST_OUT)
BUILD_METADATA_DIR := build/generated
BUILD_METADATA_HEADER := $(BUILD_METADATA_DIR)/build_metadata.hpp
BACKEND_CAPABILITIES_HEADER := $(BUILD_METADATA_DIR)/backend_capabilities.hpp
BACKEND_CAPABILITIES_GENERATOR := ../scripts/generate_backend_capabilities.py
BACKEND_CAPABILITIES_REGRESSION_TEST := ../scripts/tests/backend_capabilities_regression_test.py
BACKEND_CAPABILITIES_MAKE_REGRESSION_TEST := ../scripts/tests/backend_capabilities_make_regression_test.py
BUILD_METADATA_GENERATOR := ../scripts/generate_build_metadata.py
BUILD_METADATA_REGRESSION_TEST := ../scripts/tests/build_metadata_regression_test.py
BUILD_METADATA_MAKE_REGRESSION_TEST := ../scripts/tests/build_metadata_make_regression_test.py
SUPPORT_BUNDLE_MAINTAINER_INTAKE_INSPECTION_TEST := ../scripts/tests/support_bundle_maintainer_intake_inspection_test.py
SUPPORT_BUNDLE_MAINTAINER_PROCESSING_TEST := ../scripts/tests/support_bundle_maintainer_processing_test.py
SUPPORT_BUNDLE_RETENTION_AUDIT_TEST := ../scripts/tests/support_bundle_retention_audit_test.py
SUPPORT_BUNDLE_RETENTION_DELETION_TEST := ../scripts/tests/support_bundle_retention_deletion_test.py
SUPPORT_BUNDLE_CONTRACT_RECONCILIATION_TEST := ../scripts/tests/support_bundle_contract_reconciliation_test.py
BUILD_METADATA_REPO_ROOT := ..
BUILD_METADATA_PROJECT := $(PRJ)
BUILD_METADATA_EXECUTABLE := $(OUT)
VERSION_RELEASE_OBJECT := $(OBJ_DIR_RELEASE)/./version.o
VERSION_DEBUG_OBJECT := $(OBJ_DIR_DEBUG)/./version.o

.DEFAULT_GOAL := release

include ../scripts/build_metadata_rules.mk
include ../scripts/backend_capability_rules.mk

HOST_OS := $(shell uname -s 2>/dev/null)

# ─────────────────────────────────────────────────────────────────────────────
# Source discovery (C++ only)
# ─────────────────────────────────────────────────────────────────────────────

LOCAL_CPP_SOURCES := $(shell find . -maxdepth 1 -type f -name '*.cpp' ! -name '._*' 2>/dev/null)
ANCILLARY_GPIO_REAL_CPP_SOURCES := \
	./gpio_input.cpp \
	./gpio_output.cpp \
	./gpio_line_resolver.cpp
ANCILLARY_GPIO_STUB_CPP_SOURCES := \
	./gpio_input_unavailable.cpp \
	./gpio_output_unavailable.cpp
MACHINE_POWER_CPP_SOURCES := \
	./machine_power_control_linux.cpp \
	./machine_power_control_unavailable.cpp
LOCAL_CPP_SOURCES := $(filter-out $(ANCILLARY_GPIO_REAL_CPP_SOURCES) $(ANCILLARY_GPIO_STUB_CPP_SOURCES) $(MACHINE_POWER_CPP_SOURCES),$(LOCAL_CPP_SOURCES))
LOCAL_CPP_SOURCES += $(if $(filter 1,$(ANCILLARY_GPIO)),$(ANCILLARY_GPIO_REAL_CPP_SOURCES),$(ANCILLARY_GPIO_STUB_CPP_SOURCES))
LOCAL_CPP_SOURCES += $(if $(filter Linux,$(HOST_OS)),./machine_power_control_linux.cpp,./machine_power_control_unavailable.cpp)
COMPONENT_CPP_SOURCES := $(shell find $(COMPONENT_SRCDIRS) -type f -name '*.cpp' ! -name '._*' ! -name 'main.cpp' 2>/dev/null)
TRANSMITTER_SHARED_CPP_SOURCES := \
	./WSPR-Transmitter/src/execution_plan_compiler.cpp \
	./WSPR-Transmitter/src/transmission_controller.cpp \
	./WSPR-Transmitter/src/wspr_reference_adapter.cpp \
	./WSPR-Transmitter/src/wspr_transmit.cpp
RPI_GPIO_BACKEND_CPP_SOURCES := \
	./Mailbox/src/mailbox.cpp \
	./WSPR-Transmitter/src/wspr_transmit_backend_rpi.cpp
RP1_GPCLK_BACKEND_CPP_SOURCES := \
	./WSPR-Transmitter/src/rp1_gpclk_backend.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_event_program.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_lifecycle.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_linux_provider.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_planner.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_transition.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_transmit_backend.cpp
SI5351_BACKEND_CPP_SOURCES := \
	./WSPR-Transmitter/src/si5351_device.cpp \
	./WSPR-Transmitter/src/si5351_planner.cpp \
	./WSPR-Transmitter/src/wspr_transmit_backend_si5351.cpp
SIMULATED_BACKEND_CPP_SOURCES := \
	./WSPR-Transmitter/src/simulated_transmit_backend.cpp
TRANSMITTER_BACKEND_CPP_SOURCES := \
	$(RPI_GPIO_BACKEND_CPP_SOURCES) \
	$(RP1_GPCLK_BACKEND_CPP_SOURCES) \
	$(SI5351_BACKEND_CPP_SOURCES) \
	$(SIMULATED_BACKEND_CPP_SOURCES)
SELECTED_BACKEND_CPP_SOURCES := \
	$(if $(filter rpi-gpio,$(BACKEND_LIST)),$(RPI_GPIO_BACKEND_CPP_SOURCES)) \
	$(if $(filter rp1-gpclk,$(BACKEND_LIST)),$(RP1_GPCLK_BACKEND_CPP_SOURCES)) \
	$(if $(filter si5351,$(BACKEND_LIST)),$(SI5351_BACKEND_CPP_SOURCES)) \
	$(if $(filter simulated,$(BACKEND_LIST)),$(SIMULATED_BACKEND_CPP_SOURCES))
PPM_MANAGER_TEST_SOURCE := ./PPM-Manager/src/test.cpp
SIGNAL_HANDLER_TEST_SOURCE := ./Signal-Handler/src/test.cpp
SINGLETON_TEST_SOURCE := ./Singleton/src/test.cpp
SI5351_TEST_SOURCE := ./WSPR-Transmitter/src/si5351_backend_harness.cpp
SI5351_PLANNER_TEST_SOURCE := ./WSPR-Transmitter/src/si5351_planner_test.cpp
SI5351_TRANSITION_TEST_SOURCE := ./WSPR-Transmitter/src/si5351_transition_test.cpp
STARTUP_QUIESCE_TEST_SOURCE := ./WSPR-Transmitter/src/startup_quiesce_test.cpp
SI5351_QUALIFICATION_SOURCE := ./WSPR-Transmitter/src/si5351_startup_quiesce_qualification.cpp
SI5351_QUALIFICATION_MAIN_SOURCE := ./WSPR-Transmitter/src/si5351_startup_quiesce_qualification_main.cpp
SI5351_QUALIFICATION_TEST_SOURCE := ./WSPR-Transmitter/src/si5351_startup_quiesce_qualification_test.cpp
RP1_GPCLK_PLANNER_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_planner_test.cpp
RP1_GPCLK_LIFECYCLE_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_lifecycle_test.cpp
RP1_GPCLK_TRANSITION_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_transition_test.cpp
RP1_GPCLK_BACKEND_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_backend_test.cpp
RP1_GPCLK_LINUX_PROVIDER_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_linux_provider_test.cpp
RP1_GPCLK_TRANSMIT_BACKEND_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_transmit_backend_test.cpp
SIMULATED_BACKEND_TEST_SOURCE := ./WSPR-Transmitter/src/simulated_transmit_backend_test.cpp
SIMULATED_BACKEND_REALTIME_TEST_SOURCE := ./WSPR-Transmitter/src/simulated_transmit_backend_realtime_test.cpp
TRANSMISSION_CONTROLLER_CONTRACT_TEST_SOURCE := ./WSPR-Transmitter/src/transmission_controller_contract_test.cpp
THREAD_AFFINITY_UNAVAILABLE_TEST_SOURCE := ./WSPR-Transmitter/src/thread_affinity_unavailable_test.cpp
COMPONENT_CPP_SOURCES := $(filter-out $(PPM_MANAGER_TEST_SOURCE) $(SIGNAL_HANDLER_TEST_SOURCE) $(SINGLETON_TEST_SOURCE) $(SI5351_TEST_SOURCE) $(SI5351_PLANNER_TEST_SOURCE) $(SI5351_TRANSITION_TEST_SOURCE) $(STARTUP_QUIESCE_TEST_SOURCE) $(SI5351_QUALIFICATION_SOURCE) $(SI5351_QUALIFICATION_MAIN_SOURCE) $(SI5351_QUALIFICATION_TEST_SOURCE) $(RP1_GPCLK_PLANNER_TEST_SOURCE) $(RP1_GPCLK_LIFECYCLE_TEST_SOURCE) $(RP1_GPCLK_TRANSITION_TEST_SOURCE) $(RP1_GPCLK_BACKEND_TEST_SOURCE) $(RP1_GPCLK_LINUX_PROVIDER_TEST_SOURCE) $(RP1_GPCLK_TRANSMIT_BACKEND_TEST_SOURCE) $(SIMULATED_BACKEND_TEST_SOURCE) $(SIMULATED_BACKEND_REALTIME_TEST_SOURCE) $(TRANSMISSION_CONTROLLER_CONTRACT_TEST_SOURCE) $(THREAD_AFFINITY_UNAVAILABLE_TEST_SOURCE),$(COMPONENT_CPP_SOURCES))
COMPONENT_CPP_SOURCES := $(filter-out $(TRANSMITTER_SHARED_CPP_SOURCES) $(TRANSMITTER_BACKEND_CPP_SOURCES),$(COMPONENT_CPP_SOURCES))

CPP_SOURCES := $(LOCAL_CPP_SOURCES) $(COMPONENT_CPP_SOURCES) $(TRANSMITTER_SHARED_CPP_SOURCES) $(SELECTED_BACKEND_CPP_SOURCES)
REL_CPP_SOURCES := $(patsubst ../../%,%,$(CPP_SOURCES))

CPP_OBJECTS       := $(patsubst %.cpp, $(OBJ_DIR_RELEASE)/%.o,$(REL_CPP_SOURCES))
CPP_DEBUG_OBJECTS := $(patsubst %.cpp, $(OBJ_DIR_DEBUG)/%.o,$(REL_CPP_SOURCES))
$(CPP_OBJECTS) $(CPP_DEBUG_OBJECTS): $(BACKEND_CAPABILITIES_HEADER)
MAIN_DEBUG_OBJECT := $(OBJ_DIR_DEBUG)/./main.o
MAIN_RELEASE_OBJECT := $(OBJ_DIR_RELEASE)/./main.o
SEMANTICS_TEST_SRC := tests/dial_frequency_semantics_test.cpp
SEMANTICS_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/dial_frequency_semantics_test.o
SEMANTICS_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
CLEANUP_LIFECYCLE_TEST_SRC := tests/cleanup_lifecycle_test.cpp
CLEANUP_LIFECYCLE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/cleanup_lifecycle_test.o
CLEANUP_LIFECYCLE_TEST_DEPS := $(CLEANUP_LIFECYCLE_TEST_OBJ) $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
CLEANUP_LIFECYCLE_TEST_OUT := cleanup_lifecycle_test
UI_SOURCE_REGRESSION_TEST_SRC := tests/ui_source_regression_test.cpp
UI_SOURCE_REGRESSION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/ui_source_regression_test.o
UI_SOURCE_REGRESSION_TEST_DEPS := $(UI_SOURCE_REGRESSION_TEST_OBJ)
WEBSOCKET_LIFECYCLE_TEST_SRC := tests/websocket_lifecycle_test.cpp
WEBSOCKET_LIFECYCLE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/websocket_lifecycle_test.o
WEBSOCKET_LIFECYCLE_TEST_DEPS := $(WEBSOCKET_LIFECYCLE_TEST_OBJ) $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
WEBSOCKET_LIFECYCLE_TEST_OUT := websocket_lifecycle_test
WSPR_BAND_CATALOG_TEST_SRC := tests/wspr_band_catalog_test.cpp
WSPR_BAND_CATALOG_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/wspr_band_catalog_test.o
WSPR_BAND_CATALOG_TEST_DEPS := $(WSPR_BAND_CATALOG_TEST_OBJ) $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
WSPR_BAND_CATALOG_TEST_OUT := wspr_band_catalog_test
BAND_LOOKUP_CORRELATION_TEST_OUT := band_lookup_correlation_test
WSPR_PRESET_CATALOG_RESPONSE_TEST_OUT := wspr_preset_catalog_response_test
TEST_TONE_REQUEST_TEST_OUT := test_tone_request_test
TEST_TONE_FREQUENCY_PLAN_TEST_OUT := test_tone_frequency_plan_test
TEST_TONE_SELECTOR_PLAN_TEST_OUT := test_tone_selector_plan_test
TEST_TONE_RESPONSE_TEST_OUT := test_tone_response_test
GUARDED_MODE_CHANGE_PERSISTENCE_TEST_SRC := tests/guarded_mode_change_persistence_test.cpp
GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/guarded_mode_change_persistence_test.o
GUARDED_MODE_CHANGE_PERSISTENCE_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
BAND_GPIO_DEFAULT_CONVERGENCE_TEST_SRC := tests/band_gpio_default_convergence_test.cpp
BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/band_gpio_default_convergence_test.o
BAND_GPIO_DEFAULT_CONVERGENCE_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
BAND_GPIO_ROTATION_TEST_SRC := tests/band_gpio_rotation_test.cpp
BAND_GPIO_ROTATION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/band_gpio_rotation_test.o
BAND_GPIO_ROTATION_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
MONITOR_FILE_REGRESSION_TEST_SRC := tests/monitor_file_regression_test.cpp
MONITOR_FILE_REGRESSION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/monitor_file_regression_test.o
MONITOR_FILE_DEBUG_OBJ := $(OBJ_DIR_DEBUG)/./MonitorFile/src/monitorfile.o
MONITOR_FILE_REGRESSION_TEST_DEPS := $(MONITOR_FILE_DEBUG_OBJ)
GPIO_LOOPBACK_TEST_SRC := tests/gpio_loopback_test.cpp
GPIO_LOOPBACK_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/gpio_loopback_test.o
GPIO_LOOPBACK_TEST_DEPS := $(GPIO_LOOPBACK_TEST_OBJ)
BAND_GPIO_LIVE_MONITOR_SRC := tests/band_gpio_live_monitor.cpp
BAND_GPIO_LIVE_MONITOR_OBJ := $(OBJ_DIR_DEBUG)/tests/band_gpio_live_monitor.o
BAND_GPIO_LIVE_MONITOR_DEPS := $(BAND_GPIO_LIVE_MONITOR_OBJ)
GPIO_LINE_RESOLVER_TEST_SRC := tests/gpio_line_resolver_test.cpp
GPIO_LINE_RESOLVER_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/gpio_line_resolver_test.o
GPIO_LINE_RESOLVER_TEST_DEPS := \
	$(GPIO_LINE_RESOLVER_TEST_OBJ) \
	$(OBJ_DIR_DEBUG)/./gpio_line_resolver.o
NON_WSPR_REPEAT_POLICY_TEST_SRC := tests/non_wspr_repeat_policy_test.cpp
NON_WSPR_REPEAT_POLICY_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/non_wspr_repeat_policy_test.o
NON_WSPR_REPEAT_POLICY_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
QRSS_EXECUTION_REGRESSION_TEST_SRC := tests/qrss_execution_regression_test.cpp
QRSS_EXECUTION_REGRESSION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/qrss_execution_regression_test.o
QRSS_EXECUTION_REGRESSION_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
WSPR_TONE_REGRESSION_TEST_SRC := tests/wspr_tone_regression_test.cpp
WSPR_TONE_REGRESSION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/wspr_tone_regression_test.o
WSPR_TONE_REGRESSION_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
GPIO_BAND_POLICY_TEST_SRC := tests/gpio_band_policy_test.cpp
GPIO_BAND_POLICY_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/gpio_band_policy_test.o
GPIO_BAND_POLICY_TEST_DEPS := \
	$(GPIO_BAND_POLICY_TEST_OBJ) \
	$(OBJ_DIR_DEBUG)/./WSPR-Transmitter/src/execution_plan_compiler.o \
	$(OBJ_DIR_DEBUG)/./WSPR-Transmitter/src/transmission_controller.o
# These explicitly defined semantics-test objects can include production
# headers that consume generated capability macros. Make their generation
# barrier explicit so parallel profile builds cannot read a stale header.
$(SEMANTICS_TEST_OBJ) \
$(CLEANUP_LIFECYCLE_TEST_OBJ) \
$(UI_SOURCE_REGRESSION_TEST_OBJ) \
$(GPIO_BAND_POLICY_TEST_OBJ): $(BACKEND_CAPABILITIES_HEADER)
SELECTOR_SHUTDOWN_CLEANUP_TEST_SRC := tests/selector_shutdown_cleanup_test.cpp
SELECTOR_SHUTDOWN_CLEANUP_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/selector_shutdown_cleanup_test.o
SELECTOR_SHUTDOWN_CLEANUP_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
STANDARD_FELD_PARENT_INTEGRATION_TEST_SRC := tests/standard_feld_parent_integration_test.cpp
STANDARD_FELD_PARENT_INTEGRATION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/standard_feld_parent_integration_test.o
STANDARD_FELD_PARENT_INTEGRATION_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
STARTUP_QUIESCE_PARENT_TEST_SRC := tests/startup_quiesce_parent_test.cpp
STARTUP_QUIESCE_PARENT_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/startup_quiesce_parent_test.o
STARTUP_QUIESCE_PARENT_TEST_DEPS := $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
BACKEND_PROFILE_FACTORY_TEST_SRC := tests/backend_profile_factory_test.cpp
BACKEND_PROFILE_FACTORY_TEST_OBJ := $(OBJ_DIR_RELEASE)/tests/backend_profile_factory_test.o
BACKEND_PROFILE_FACTORY_TEST_DEPS := $(BACKEND_PROFILE_FACTORY_TEST_OBJ) $(filter-out $(MAIN_RELEASE_OBJECT),$(CPP_OBJECTS))
BACKEND_PROFILE_FACTORY_TEST_BIN := $(PROFILE_BIN_DIR)/$(BACKEND_PROFILE_FACTORY_TEST_OUT)
MACHINE_POWER_UNAVAILABLE_TEST_SRC := tests/machine_power_control_unavailable_test.cpp
MACHINE_POWER_UNAVAILABLE_TEST_BIN := $(BIN_DIR)/$(MACHINE_POWER_UNAVAILABLE_TEST_OUT)
MACHINE_POWER_REGRESSION_TEST := tests/machine_power_control_regression_test.py
QUALIFICATION_GPIO_TEST_MODE_TEST_SRC := tests/qualification_gpio_test_mode_test.cpp
QUALIFICATION_GPIO_TEST_MODE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/qualification_gpio_test_mode_test.o
QUALIFICATION_GPIO_TEST_MODE_TEST_DEPS := $(QUALIFICATION_GPIO_TEST_MODE_TEST_OBJ)
SUPPORT_REQUEST_GUARD_TEST_SRC := tests/support_request_guard_test.cpp
SUPPORT_REQUEST_GUARD_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_request_guard_test.o
SUPPORT_REQUEST_GUARD_TEST_DEPS := $(SUPPORT_REQUEST_GUARD_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_request_guard.o $(OBJ_DIR_DEBUG)/./privileged_network_policy.o
SUPPORT_BUNDLE_JOB_MANAGER_TEST_SRC := tests/support_bundle_job_manager_test.cpp
SUPPORT_BUNDLE_JOB_MANAGER_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_job_manager_test.o
SUPPORT_BUNDLE_JOB_MANAGER_TEST_DEPS := $(SUPPORT_BUNDLE_JOB_MANAGER_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_job_manager.o $(OBJ_DIR_DEBUG)/./support_bundle_result_validator.o $(OBJ_DIR_DEBUG)/./support_bundle_job_directory_remover.o $(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o
SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_SRC := tests/support_bundle_result_validator_test.cpp
SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_result_validator_test.o
SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_DEPS := $(SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_result_validator.o
SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_SRC := tests/support_bundle_collector_executor_test.cpp
SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_collector_executor_test.o
SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_DEPS := $(SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_collector_executor.o $(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o
SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_HELPER_SRC := tests/support_bundle_collector_executor_helper.cpp
SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_HELPER_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_collector_executor_helper.o
SUPPORT_BUNDLE_RUNTIME_TEST_SRC := tests/support_bundle_runtime_test.cpp
SUPPORT_BUNDLE_RUNTIME_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_runtime_test.o
SUPPORT_BUNDLE_RUNTIME_TEST_DEPS := $(SUPPORT_BUNDLE_RUNTIME_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_runtime.o $(OBJ_DIR_DEBUG)/./support_bundle_startup_cleanup.o $(OBJ_DIR_DEBUG)/./support_bundle_job_manager.o $(OBJ_DIR_DEBUG)/./support_bundle_result_validator.o $(OBJ_DIR_DEBUG)/./support_bundle_job_directory_remover.o $(OBJ_DIR_DEBUG)/./support_bundle_collector_executor.o $(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o
SUPPORT_BUNDLE_HTTP_TEST_SRC := tests/support_bundle_http_test.cpp
SUPPORT_BUNDLE_HTTP_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_http_test.o
SUPPORT_BUNDLE_HTTP_TEST_DEPS := $(SUPPORT_BUNDLE_HTTP_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_http.o $(OBJ_DIR_DEBUG)/./support_bundle_download_preparation.o $(OBJ_DIR_DEBUG)/./support_bundle_archive_digest.o $(OBJ_DIR_DEBUG)/./support_bundle_checksum_file.o $(OBJ_DIR_DEBUG)/./support_bundle_download_file.o $(OBJ_DIR_DEBUG)/./support_bundle_job_manager.o $(OBJ_DIR_DEBUG)/./support_bundle_result_validator.o $(OBJ_DIR_DEBUG)/./support_bundle_job_directory_remover.o $(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o $(OBJ_DIR_DEBUG)/./support_request_guard.o $(OBJ_DIR_DEBUG)/./privileged_network_policy.o $(OBJ_DIR_DEBUG)/./httplib.o
SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_SRC := tests/support_bundle_web_server_wiring_test.cpp
SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_web_server_wiring_test.o
SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_DEPS := $(SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_OBJ)
SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_SRC := tests/support_bundle_download_file_test.cpp
SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_download_file_test.o
SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_DEPS := $(SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_download_file.o
SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_SRC := tests/support_bundle_checksum_file_test.cpp
SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_checksum_file_test.o
SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_DEPS := $(SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_checksum_file.o
SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_SRC := tests/support_bundle_archive_digest_test.cpp
SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_archive_digest_test.o
SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_DEPS := $(SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_archive_digest.o $(OBJ_DIR_DEBUG)/./support_bundle_download_file.o
SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_SRC := tests/support_bundle_download_preparation_test.cpp
SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_download_preparation_test.o
SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_DEPS := $(SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_download_preparation.o $(OBJ_DIR_DEBUG)/./support_bundle_archive_digest.o $(OBJ_DIR_DEBUG)/./support_bundle_checksum_file.o $(OBJ_DIR_DEBUG)/./support_bundle_download_file.o
SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_SRC := tests/support_bundle_job_directory_remover_test.cpp
SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_job_directory_remover_test.o
SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_DEPS := $(SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_job_directory_remover.o
SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_SRC := tests/support_bundle_startup_cleanup_test.cpp
SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_startup_cleanup_test.o
SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_DEPS := $(SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_startup_cleanup.o $(OBJ_DIR_DEBUG)/./support_bundle_job_directory_remover.o
SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_SRC := tests/support_bundle_private_artifact_test.cpp
SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_private_artifact_test.o
SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_DEPS := $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o
SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_SRC := tests/support_bundle_age_roundtrip_test.cpp
SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_age_roundtrip_test.o
SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_DEPS := $(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o
SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_SRC := tests/support_bundle_intake_validation_test.cpp
SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_intake_validation_test.o
SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_DEPS := $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_intake_validation.o
SUPPORT_BUNDLE_INTAKE_STATE_TEST_SRC := tests/support_bundle_intake_state_test.cpp
SUPPORT_BUNDLE_INTAKE_STATE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_intake_state_test.o
SUPPORT_BUNDLE_INTAKE_STATE_TEST_DEPS := $(SUPPORT_BUNDLE_INTAKE_STATE_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_intake_state.o
SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_SRC := tests/support_bundle_intake_retrieval_test.cpp
SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_intake_retrieval_test.o
SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_DEPS := $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_intake_retrieval.o
SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_SRC := tests/support_bundle_intake_retrieval_helper.cpp
SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_intake_retrieval_helper.o
SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_SRC := tests/support_bundle_intake_controller_test.cpp
SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_intake_controller_test.o
SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_DEPS := $(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_intake_controller.o $(OBJ_DIR_DEBUG)/./support_bundle_intake_retrieval.o $(OBJ_DIR_DEBUG)/./support_bundle_intake_validation.o $(OBJ_DIR_DEBUG)/./support_bundle_intake_state.o
SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_SRC := tests/support_bundle_intake_runtime_test.cpp
SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_intake_runtime_test.o
SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_DEPS := $(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_intake_runtime.o $(OBJ_DIR_DEBUG)/./support_bundle_intake_controller.o $(OBJ_DIR_DEBUG)/./support_bundle_intake_retrieval.o $(OBJ_DIR_DEBUG)/./support_bundle_intake_validation.o $(OBJ_DIR_DEBUG)/./support_bundle_intake_state.o
SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_SRC := tests/support_bundle_intake_production_test.cpp
SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/support_bundle_intake_production_test.o
SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_DEPS := $(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_intake_production.o $(OBJ_DIR_DEBUG)/./support_bundle_intake_runtime.o

# ─────────────────────────────────────────────────────────────────────────────
# Dependency includes
# ─────────────────────────────────────────────────────────────────────────────

DEPFILES := $(wildcard $(DEP_DIR)/*.d $(DEP_DIR)/*/*.d)
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

# Keep Linux's memory-aware sizing policy, but do not run Linux-only probes on
# other hosts. Darwin supplies its logical CPU count through sysctl; unknown
# hosts use the POSIX getconf interface when available. All non-Linux paths
# fall back to one job if processor discovery is unavailable or invalid.
ifeq ($(HOST_OS),Linux)
MEM_KB       := $(shell awk '/^MemTotal:/ {print $$2}' /proc/meminfo)
MEM_AVAIL_KB := $(shell awk '/^MemAvailable:/ {print $$2}' /proc/meminfo)
SWAP_KB      := $(shell awk '/^SwapTotal:/ {print $$2}' /proc/meminfo)
NPROCS       := $(shell n=$$(nproc 2>/dev/null); if [ "$$n" -ge 1 ] 2>/dev/null; then echo "$$n"; else echo 1; fi)
else ifeq ($(HOST_OS),Darwin)
NPROCS       := $(shell n=$$(sysctl -n hw.logicalcpu 2>/dev/null); if [ "$$n" -ge 1 ] 2>/dev/null; then echo "$$n"; else echo 1; fi)
else
NPROCS       := $(shell n=$$(getconf _NPROCESSORS_ONLN 2>/dev/null); if [ "$$n" -ge 1 ] 2>/dev/null; then echo "$$n"; else echo 1; fi)
endif

ONE_POINT_FIVE_GIB_KB   := $(shell expr 1536 \* 1024)
THREE_POINT_FIVE_GIB_KB := $(shell expr 3584 \* 1024)

# If available memory is below this threshold, force single-job builds.
# 1280 MiB is a safer default for C++ builds when a desktop is running.
MIN_MEM_AVAIL_KB ?= $(shell expr 1280 \* 1024)

JOBS_COMPUTED :=

ifeq ($(HOST_OS),Linux)
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
PKG_CFLAGS :=
PKG_LIBS   :=
ifeq ($(ANCILLARY_GPIO),1)
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

ifeq ($(shell [ "$(GPIOD_API_MAJOR)" -ge 2 ] 2>/dev/null && echo yes),yes)
GPIOD_HAS_V2 := 1
else
GPIOD_HAS_V2 := 0
endif
else
GPIOD_API_MAJOR := 0
GPIOD_HAS_V2 := 0
endif

# ─────────────────────────────────────────────────────────────────────────────
# Flags
# ─────────────────────────────────────────────────────────────────────────────

CXX_VERSION_LINE := $(shell $(CXX) --version 2>/dev/null | head -n 1)
ifneq ($(findstring clang,$(CXX_VERSION_LINE)),)
COMPILER_DIAGNOSTIC_FLAGS :=
else
COMPILER_DIAGNOSTIC_FLAGS := -fmax-errors=10
endif

COMMON_FLAGS := -Wall -Werror $(COMPILER_DIAGNOSTIC_FLAGS) -MMD -MP

# Base compiler flags
CXXFLAGS := -Wno-psabi -std=c++$(CXXVER) \
           $(COMMON_FLAGS) \
           $(PKG_CFLAGS) \
           $(OPENSSL_CFLAGS) \
           $(SYSTEMD_CFLAGS) \
           -DGPIOD_API_MAJOR=$(GPIOD_API_MAJOR) \
           -DLIB_JOURNALD=$(HAVE_JOURNALD)

# Include paths
CXXFLAGS += -I$(abspath .)
CXXFLAGS += -I$(abspath $(BUILD_METADATA_DIR))
CXXFLAGS += $(foreach dir,$(COMPONENT_SRCDIRS),-I$(abspath $(dir)))
CXXFLAGS += -I$(abspath ./WSPR-Reference/src/wspr)

# Debug/release variants
CXX_DEBUG_FLAGS   := $(CXXFLAGS) -g $(DEBUG_DEFS)
CXX_RELEASE_FLAGS := $(CXXFLAGS) -O2

# Linker flags
ifeq ($(HOST_OS),Linux)
ATOMIC_LIB := -latomic
else
ATOMIC_LIB :=
endif

LDFLAGS := -lpthread $(ATOMIC_LIB) \
           $(PKG_LIBS) \
           $(OPENSSL_LIBS) \
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

define MAKE_DEBUG_COMPONENT_RULES
$(OBJ_DIR_DEBUG)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.cpp
	$(Q)mkdir -p $$(@D)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)echo "Compiling (debug) $$< into $$@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/$$*.d -c $$< -o $$@
endef
$(foreach dir,$(COMPONENT_SRCDIRS),$(eval $(call MAKE_DEBUG_COMPONENT_RULES,$(dir))))

# Link debug binary
$(PROFILE_DEBUG_BIN): $(CPP_DEBUG_OBJECTS)
	$(Q)mkdir -p $(PROFILE_BIN_DIR)
	$(Q)echo "Linking debug: $(TEST_OUT) (libgpiod major: $(GPIOD_API_MAJOR))"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: FORCE_PROFILE_BINARY
FORCE_PROFILE_BINARY:

$(BIN_DIR)/$(TEST_OUT): $(PROFILE_DEBUG_BIN) FORCE_PROFILE_BINARY
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)cp $(PROFILE_DEBUG_BIN) $@

$(SEMANTICS_TEST_OBJ): $(SEMANTICS_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/dial_frequency_semantics_test.d -c $< -o $@

$(BIN_DIR)/$(SEMANTICS_TEST_OUT): $(SEMANTICS_TEST_OBJ) $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(SEMANTICS_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(CLEANUP_LIFECYCLE_TEST_OBJ): $(CLEANUP_LIFECYCLE_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/cleanup_lifecycle_test.d -c $< -o $@

$(BIN_DIR)/$(CLEANUP_LIFECYCLE_TEST_OUT): $(CLEANUP_LIFECYCLE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(CLEANUP_LIFECYCLE_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(UI_SOURCE_REGRESSION_TEST_OBJ): $(UI_SOURCE_REGRESSION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/ui_source_regression_test.d -c $< -o $@

$(BIN_DIR)/$(UI_SOURCE_REGRESSION_TEST_OUT): $(UI_SOURCE_REGRESSION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(UI_SOURCE_REGRESSION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(WEBSOCKET_LIFECYCLE_TEST_OUT): $(WEBSOCKET_LIFECYCLE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(WSPR_BAND_CATALOG_TEST_OBJ): $(WSPR_BAND_CATALOG_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/wspr_band_catalog_test.d -c $< -o $@

$(BIN_DIR)/$(WSPR_BAND_CATALOG_TEST_OUT): $(WSPR_BAND_CATALOG_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(WSPR_BAND_CATALOG_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(BAND_LOOKUP_CORRELATION_TEST_OUT): tests/band_lookup_correlation_test.cpp band_lookup.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(WSPR_PRESET_CATALOG_RESPONSE_TEST_OUT): tests/wspr_preset_catalog_response_test.cpp band_lookup.cpp wspr_band_catalog_response.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_TONE_REQUEST_TEST_OUT): tests/test_tone_request_test.cpp test_tone_request.cpp band_lookup.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(TEST_TONE_FREQUENCY_PLAN_TEST_OUT): tests/test_tone_frequency_plan_test.cpp test_tone_frequency_plan.cpp band_lookup.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)
$(BIN_DIR)/$(TEST_TONE_SELECTOR_PLAN_TEST_OUT): tests/test_tone_selector_plan_test.cpp test_tone_selector_plan.cpp band_lookup.cpp INI-Handler/src/ini_file.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)
$(BIN_DIR)/$(TEST_TONE_RESPONSE_TEST_OUT): tests/test_tone_response_test.cpp $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OBJ): $(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/guarded_mode_change_persistence_test.d -c $< -o $@

$(BIN_DIR)/$(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OUT): $(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OBJ) $(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OBJ): $(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/band_gpio_default_convergence_test.d -c $< -o $@

$(BIN_DIR)/$(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OUT): $(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OBJ) $(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BAND_GPIO_ROTATION_TEST_OBJ): $(BAND_GPIO_ROTATION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/band_gpio_rotation_test.d -c $< -o $@

$(BIN_DIR)/$(BAND_GPIO_ROTATION_TEST_OUT): $(BAND_GPIO_ROTATION_TEST_OBJ) $(BAND_GPIO_ROTATION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(BAND_GPIO_ROTATION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(MONITOR_FILE_REGRESSION_TEST_OBJ): $(MONITOR_FILE_REGRESSION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/monitor_file_regression_test.d -c $< -o $@

$(BIN_DIR)/$(MONITOR_FILE_REGRESSION_TEST_OUT): $(MONITOR_FILE_REGRESSION_TEST_OBJ) $(MONITOR_FILE_REGRESSION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(MONITOR_FILE_REGRESSION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(GPIO_LINE_RESOLVER_TEST_OBJ): $(GPIO_LINE_RESOLVER_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/gpio_line_resolver_test.d -c $< -o $@

$(BIN_DIR)/$(GPIO_LINE_RESOLVER_TEST_OUT): $(GPIO_LINE_RESOLVER_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(GPIO_LINE_RESOLVER_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(NON_WSPR_REPEAT_POLICY_TEST_OBJ): $(NON_WSPR_REPEAT_POLICY_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/non_wspr_repeat_policy_test.d -c $< -o $@

$(BIN_DIR)/$(NON_WSPR_REPEAT_POLICY_TEST_OUT): $(NON_WSPR_REPEAT_POLICY_TEST_OBJ) $(NON_WSPR_REPEAT_POLICY_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(NON_WSPR_REPEAT_POLICY_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(QRSS_EXECUTION_REGRESSION_TEST_OBJ): $(QRSS_EXECUTION_REGRESSION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/qrss_execution_regression_test.d -c $< -o $@

$(BIN_DIR)/$(QRSS_EXECUTION_REGRESSION_TEST_OUT): $(QRSS_EXECUTION_REGRESSION_TEST_OBJ) $(QRSS_EXECUTION_REGRESSION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(QRSS_EXECUTION_REGRESSION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(WSPR_TONE_REGRESSION_TEST_OBJ): $(WSPR_TONE_REGRESSION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/wspr_tone_regression_test.d -c $< -o $@

$(BIN_DIR)/$(WSPR_TONE_REGRESSION_TEST_OUT): $(WSPR_TONE_REGRESSION_TEST_OBJ) $(WSPR_TONE_REGRESSION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(WSPR_TONE_REGRESSION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(GPIO_BAND_POLICY_TEST_OBJ): $(GPIO_BAND_POLICY_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/gpio_band_policy_test.d -c $< -o $@

$(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT): $(GPIO_BAND_POLICY_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(GPIO_BAND_POLICY_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(SELECTOR_SHUTDOWN_CLEANUP_TEST_OBJ): $(SELECTOR_SHUTDOWN_CLEANUP_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/selector_shutdown_cleanup_test.d -c $< -o $@

$(BIN_DIR)/$(SELECTOR_SHUTDOWN_CLEANUP_TEST_OUT): $(SELECTOR_SHUTDOWN_CLEANUP_TEST_OBJ) $(SELECTOR_SHUTDOWN_CLEANUP_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(SELECTOR_SHUTDOWN_CLEANUP_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(STANDARD_FELD_PARENT_INTEGRATION_TEST_OBJ): $(STANDARD_FELD_PARENT_INTEGRATION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/standard_feld_parent_integration_test.d -c $< -o $@

$(BIN_DIR)/$(STANDARD_FELD_PARENT_INTEGRATION_TEST_OUT): $(STANDARD_FELD_PARENT_INTEGRATION_TEST_OBJ) $(STANDARD_FELD_PARENT_INTEGRATION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(STANDARD_FELD_PARENT_INTEGRATION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(STARTUP_QUIESCE_PARENT_TEST_OBJ): $(STARTUP_QUIESCE_PARENT_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/startup_quiesce_parent_test.d -c $< -o $@

$(BIN_DIR)/startup_quiesce_parent_test: $(STARTUP_QUIESCE_PARENT_TEST_OBJ) $(STARTUP_QUIESCE_PARENT_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BACKEND_PROFILE_FACTORY_TEST_OBJ): $(BACKEND_PROFILE_FACTORY_TEST_SRC) $(BACKEND_CAPABILITIES_HEADER)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/tests/backend_profile_factory_test.d -c $< -o $@

$(BACKEND_PROFILE_FACTORY_TEST_BIN): $(BACKEND_PROFILE_FACTORY_TEST_DEPS)
	$(Q)mkdir -p $(PROFILE_BIN_DIR)
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) $^ -o $@ $(LDFLAGS)

$(QUALIFICATION_GPIO_TEST_MODE_TEST_OBJ): $(QUALIFICATION_GPIO_TEST_MODE_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/qualification_gpio_test_mode_test.d -c $< -o $@

$(BIN_DIR)/$(QUALIFICATION_GPIO_TEST_MODE_TEST_OUT): $(QUALIFICATION_GPIO_TEST_MODE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(QUALIFICATION_GPIO_TEST_MODE_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_REQUEST_GUARD_TEST_OUT): $(SUPPORT_REQUEST_GUARD_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_JOB_MANAGER_TEST_OUT): $(SUPPORT_BUNDLE_JOB_MANAGER_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_OUT): $(SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_OUT): $(SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_HELPER_OUT): $(SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_HELPER_OBJ)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_RUNTIME_TEST_OUT): $(SUPPORT_BUNDLE_RUNTIME_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_HTTP_TEST_OUT): $(SUPPORT_BUNDLE_HTTP_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_OUT): $(SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_OUT): $(SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_OUT): $(SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_OUT): $(SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_OUT): $(SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_OUT): $(SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_OUT): $(SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_OBJ): $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_private_artifact_test.d -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o: support_bundle_private_artifact.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) -MF $(DEP_DIR)/support_bundle_private_artifact.d -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_OUT): $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OBJ): $(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_age_roundtrip_test.d -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OUT): $(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_intake_validation_test.d -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_validation.o: support_bundle_intake_validation.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/support_bundle_intake_validation.d -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_STATE_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_STATE_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_intake_state_test.d -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_state.o: support_bundle_intake_state.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/support_bundle_intake_state.d -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_STATE_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_STATE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_intake_retrieval_test.d -c $< -o $@

$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OBJ): $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_intake_retrieval_helper.d -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_retrieval.o: support_bundle_intake_retrieval.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/support_bundle_intake_retrieval.d -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OUT): $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OBJ)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_intake_controller_test.d -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_controller.o: support_bundle_intake_controller.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/support_bundle_intake_controller.d -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_intake_runtime_test.d -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_runtime.o: support_bundle_intake_runtime.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/support_bundle_intake_runtime.d -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_production.o: support_bundle_intake_production.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/support_bundle_intake_production.d -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(DEP_DIR)/tests/support_bundle_intake_production_test.d -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

ifeq ($(GPIOD_HAS_V2),1)
$(GPIO_LOOPBACK_TEST_OBJ): $(GPIO_LOOPBACK_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/gpio_loopback_test.d -c $< -o $@

$(BIN_DIR)/$(GPIO_LOOPBACK_TEST_OUT): $(GPIO_LOOPBACK_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(GPIO_LOOPBACK_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BAND_GPIO_LIVE_MONITOR_OBJ): $(BAND_GPIO_LIVE_MONITOR_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/tests
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(DEP_DIR)/tests/band_gpio_live_monitor.d -c $< -o $@

$(BIN_DIR)/$(BAND_GPIO_LIVE_MONITOR_OUT): $(BAND_GPIO_LIVE_MONITOR_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(BAND_GPIO_LIVE_MONITOR_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)
endif

# ─────────────────────────────────────────────────────────────────────────────
# Compile rules (release)
# ─────────────────────────────────────────────────────────────────────────────

$(OBJ_DIR_RELEASE)/%.o: %.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $*)
	$(Q)echo "Compiling (release) $< into $@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/$*.d -c $< -o $@

define MAKE_RELEASE_COMPONENT_RULES
$(OBJ_DIR_RELEASE)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.cpp
	$(Q)mkdir -p $$(@D)
	$(Q)mkdir -p $(DEP_DIR)/$(dir $$*)
	$(Q)echo "Compiling (release) $$< into $$@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(DEP_DIR)/$$*.d -c $$< -o $$@
endef
$(foreach dir,$(COMPONENT_SRCDIRS),$(eval $(call MAKE_RELEASE_COMPONENT_RULES,$(dir))))

# Link release binary
$(PROFILE_RELEASE_BIN): $(CPP_OBJECTS)
	$(Q)mkdir -p $(PROFILE_BIN_DIR)
	$(Q)echo "Linking release: $(OUT) (libgpiod major: $(GPIOD_API_MAJOR))"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(OUT): $(PROFILE_RELEASE_BIN) FORCE_PROFILE_BINARY
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)cp $(PROFILE_RELEASE_BIN) $@

# ─────────────────────────────────────────────────────────────────────────────
# Phony targets
# ─────────────────────────────────────────────────────────────────────────────

.PHONY: clean debug test test-tone test-oneshot install-binary FORCE_PROFILE_BINARY
.PHONY: semantics-test machine-power-control-test websocket-lifecycle-test wspr-band-catalog-test gpio-band-policy-test
.PHONY: support-request-guard-test privileged-network-policy-test backend-http-guard-test websocket-upgrade-guard-test apache-privileged-network-policy-test privileged-network-transaction-test privileged-network-runtime-test privileged-network-runtime-wiring-test privileged-network-admin-test support-bundle-job-manager-test support-bundle-result-validator-test
.PHONY: support-bundle-collector-executor-test support-bundle-runtime-test support-bundle-runtime-installer-test
.PHONY: installer-dependency-test support-bundle-age-dependency-test support-bundle-http-test support-bundle-web-server-wiring-test
.PHONY: support-bundle-download-file-test support-bundle-checksum-file-test support-bundle-archive-digest-test
.PHONY: support-bundle-download-preparation-test support-bundle-job-directory-remover-test support-bundle-startup-cleanup-test
.PHONY: support-bundle-private-artifact-test support-bundle-age-roundtrip-test support-bundle-key-provisioning-test
.PHONY: support-bundle-production-encryption-metadata-test
.PHONY: support-bundle-signing-key-provisioning-test support-bundle-intake-manifest-preparation-test
.PHONY: support-bundle-intake-manifest-lifecycle-test support-bundle-intake-production-manifest-test
.PHONY: support-bundle-intake-publication-commit-test support-bundle-intake-publication-push-test
.PHONY: support-bundle-intake-publication-verification-test support-bundle-intake-trust-compilation-test
.PHONY: support-bundle-intake-production-trust-test support-bundle-intake-identity-ceremony-preflight-test
.PHONY: support-bundle-maintainer-intake-inspection-test support-bundle-maintainer-processing-test support-bundle-retention-audit-test support-bundle-retention-deletion-test
.PHONY: support-bundle-contract-reconciliation-test
.PHONY: support-bundle-intake-validation-test support-bundle-intake-state-test support-bundle-intake-retrieval-test
.PHONY: support-bundle-intake-controller-test support-bundle-intake-runtime-test support-bundle-intake-production-test
.PHONY: build-dependency-regression-test backend-capabilities-regression-test backend-capabilities-make-regression-test
.PHONY: backend-profile-factory-test update-check-comparison-test guarded-mode-change-persistence-test
.PHONY: band-gpio-default-convergence-test band-gpio-rotation-test selector-shutdown-cleanup-test standard-feld-parent-integration-test standard-feld-gpio-dry-run-test standard-feld-rpi-execution-test
.PHONY: qualification-gpio-test-mode-test monitor-file-regression-test gpio-w
.PHONY: rp1-gpclk-planner-test rp1-gpclk-lifecycle-test rp1-gpclk-transition-test
DEBUG: debug
RELEASE: release

clean:
	$(Q)echo "Cleaning up build artifacts."
	$(Q)$(SUDO) rm -rf build
	$(Q)$(SUDO) rm -rf ../executables

release: $(BIN_DIR)/$(OUT)

ifeq ($(BUILD_METADATA_QUERY),)
	$(Q)echo "Release build completed successfully."
endif

build-metadata-regression-test:
	$(Q)python3 $(BUILD_METADATA_REGRESSION_TEST)

build-metadata-make-regression-test:
	$(Q)python3 $(BUILD_METADATA_MAKE_REGRESSION_TEST)

backend-capabilities-regression-test:
	$(Q)python3 $(BACKEND_CAPABILITIES_REGRESSION_TEST)

backend-capabilities-make-regression-test:
	$(Q)python3 $(BACKEND_CAPABILITIES_MAKE_REGRESSION_TEST)

backend-profile-factory-test: $(BACKEND_PROFILE_FACTORY_TEST_BIN)
	$(Q)./$(BACKEND_PROFILE_FACTORY_TEST_BIN)

$(MACHINE_POWER_UNAVAILABLE_TEST_BIN): $(MACHINE_POWER_UNAVAILABLE_TEST_SRC) machine_power_control_unavailable.cpp machine_power_control.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++$(CXXVER) -Wall -Wextra -Werror $(MACHINE_POWER_UNAVAILABLE_TEST_SRC) machine_power_control_unavailable.cpp -o $@

machine-power-control-test: $(MACHINE_POWER_UNAVAILABLE_TEST_BIN)
	$(Q)./$(MACHINE_POWER_UNAVAILABLE_TEST_BIN)
	$(Q)python3 $(MACHINE_POWER_REGRESSION_TEST)

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

semantics-test: $(BIN_DIR)/$(SEMANTICS_TEST_OUT) $(BIN_DIR)/$(CLEANUP_LIFECYCLE_TEST_OUT) $(BIN_DIR)/$(UI_SOURCE_REGRESSION_TEST_OUT) $(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT)
	$(Q)echo "Running runtime semantics tests."
	$(Q)./$(BIN_DIR)/$(SEMANTICS_TEST_OUT)
	$(Q)echo "Running cleanup lifecycle tests."
	$(Q)./$(BIN_DIR)/$(CLEANUP_LIFECYCLE_TEST_OUT)
	$(Q)echo "Running UI/source regression tests."
	$(Q)./$(BIN_DIR)/$(UI_SOURCE_REGRESSION_TEST_OUT)
	$(Q)echo "Running GPIO band fail-closed policy tests."
	$(Q)./$(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT)
	$(Q)echo "Running log timestamp display regression tests."
	$(Q)node tests/log_timestamp_display_test.js
	$(Q)echo "Running update-check comparison tests."
	$(Q)node tests/update_check_comparison_test.js

websocket-lifecycle-test: $(BIN_DIR)/$(WEBSOCKET_LIFECYCLE_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(WEBSOCKET_LIFECYCLE_TEST_OUT)

wspr-band-catalog-test: $(BIN_DIR)/$(WSPR_BAND_CATALOG_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(WSPR_BAND_CATALOG_TEST_OUT)

band-lookup-correlation-test: $(BIN_DIR)/$(BAND_LOOKUP_CORRELATION_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(BAND_LOOKUP_CORRELATION_TEST_OUT)

wspr-preset-catalog-response-test: $(BIN_DIR)/$(WSPR_PRESET_CATALOG_RESPONSE_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(WSPR_PRESET_CATALOG_RESPONSE_TEST_OUT)

test-tone-request-test: $(BIN_DIR)/$(TEST_TONE_REQUEST_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(TEST_TONE_REQUEST_TEST_OUT)

test-tone-frequency-plan-test: $(BIN_DIR)/$(TEST_TONE_FREQUENCY_PLAN_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(TEST_TONE_FREQUENCY_PLAN_TEST_OUT)
test-tone-selector-plan-test: $(BIN_DIR)/$(TEST_TONE_SELECTOR_PLAN_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(TEST_TONE_SELECTOR_PLAN_TEST_OUT)
test-tone-response-test: $(BIN_DIR)/$(TEST_TONE_RESPONSE_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(TEST_TONE_RESPONSE_TEST_OUT)

support-request-guard-test: $(BIN_DIR)/$(SUPPORT_REQUEST_GUARD_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_REQUEST_GUARD_TEST_OUT)

$(BIN_DIR)/$(PRIVILEGED_NETWORK_POLICY_TEST_OUT): tests/privileged_network_policy_test.cpp privileged_network_policy.cpp privileged_network_policy.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) tests/privileged_network_policy_test.cpp privileged_network_policy.cpp -o $@ $(LDFLAGS)

privileged-network-policy-test: $(BIN_DIR)/$(PRIVILEGED_NETWORK_POLICY_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(PRIVILEGED_NETWORK_POLICY_TEST_OUT)

$(BIN_DIR)/$(BACKEND_HTTP_GUARD_TEST_OUT): tests/backend_http_guard_test.cpp backend_http_guard.cpp privileged_network_policy.cpp support_request_guard.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

backend-http-guard-test: $(BIN_DIR)/$(BACKEND_HTTP_GUARD_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(BACKEND_HTTP_GUARD_TEST_OUT)

$(BIN_DIR)/$(WEBSOCKET_UPGRADE_GUARD_TEST_OUT): tests/websocket_upgrade_guard_test.cpp websocket_upgrade_guard.cpp support_request_guard.cpp privileged_network_policy.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

websocket-upgrade-guard-test: $(BIN_DIR)/$(WEBSOCKET_UPGRADE_GUARD_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(WEBSOCKET_UPGRADE_GUARD_TEST_OUT)

$(BIN_DIR)/$(APACHE_PRIVILEGED_NETWORK_POLICY_TEST_OUT): tests/apache_privileged_network_policy_test.cpp apache_privileged_network_policy.cpp apache_privileged_network_policy.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) tests/apache_privileged_network_policy_test.cpp apache_privileged_network_policy.cpp -o $@ $(LDFLAGS)

apache-privileged-network-policy-test: $(BIN_DIR)/$(APACHE_PRIVILEGED_NETWORK_POLICY_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(APACHE_PRIVILEGED_NETWORK_POLICY_TEST_OUT)

$(BIN_DIR)/$(PRIVILEGED_NETWORK_TRANSACTION_TEST_OUT): tests/privileged_network_transaction_test.cpp privileged_network_transaction.cpp apache_privileged_network_policy.cpp privileged_network_policy.cpp INI-Handler/src/ini_file.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

privileged-network-transaction-test: $(BIN_DIR)/$(PRIVILEGED_NETWORK_TRANSACTION_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(PRIVILEGED_NETWORK_TRANSACTION_TEST_OUT)

$(BIN_DIR)/$(PRIVILEGED_NETWORK_RUNTIME_TEST_OUT): tests/privileged_network_runtime_test.cpp privileged_network_runtime.cpp privileged_network_policy.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

privileged-network-runtime-test: $(BIN_DIR)/$(PRIVILEGED_NETWORK_RUNTIME_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(PRIVILEGED_NETWORK_RUNTIME_TEST_OUT)

$(BIN_DIR)/$(PRIVILEGED_NETWORK_RUNTIME_WIRING_TEST_OUT): tests/privileged_network_runtime_wiring_test.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

privileged-network-runtime-wiring-test: $(BIN_DIR)/$(PRIVILEGED_NETWORK_RUNTIME_WIRING_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(PRIVILEGED_NETWORK_RUNTIME_WIRING_TEST_OUT)

$(BIN_DIR)/$(PRIVILEGED_NETWORK_ADMIN_TEST_OUT): tests/privileged_network_admin_test.cpp privileged_network_admin.cpp privileged_network_transaction.cpp apache_privileged_network_policy.cpp privileged_network_runtime.cpp privileged_network_policy.cpp support_request_guard.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

privileged-network-admin-test: $(BIN_DIR)/$(PRIVILEGED_NETWORK_ADMIN_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(PRIVILEGED_NETWORK_ADMIN_TEST_OUT)

support-bundle-job-manager-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_JOB_MANAGER_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_JOB_MANAGER_TEST_OUT)

support-bundle-result-validator-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_RESULT_VALIDATOR_TEST_OUT)

support-bundle-collector-executor-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_OUT) $(BIN_DIR)/$(SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_HELPER_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_COLLECTOR_EXECUTOR_TEST_OUT)

support-bundle-runtime-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_RUNTIME_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_RUNTIME_TEST_OUT)

support-bundle-runtime-installer-test:
	$(Q)bash ../scripts/tests/support_bundle_runtime_install_test.sh

installer-dependency-test:
	$(Q)bash ../scripts/tests/installer_dependency_test.sh

support-bundle-age-dependency-test:
	$(Q)bash ../scripts/tests/support_bundle_age_dependency_test.sh

support-bundle-http-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_HTTP_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_HTTP_TEST_OUT)

support-bundle-web-server-wiring-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_WEB_SERVER_WIRING_TEST_OUT)

support-bundle-download-file-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_DOWNLOAD_FILE_TEST_OUT)

support-bundle-checksum-file-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_CHECKSUM_FILE_TEST_OUT)

support-bundle-archive-digest-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_ARCHIVE_DIGEST_TEST_OUT)

support-bundle-download-preparation-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_DOWNLOAD_PREPARATION_TEST_OUT)

support-bundle-job-directory-remover-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_JOB_DIRECTORY_REMOVER_TEST_OUT)

support-bundle-startup-cleanup-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_STARTUP_CLEANUP_TEST_OUT)

support-bundle-private-artifact-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_OUT)

support-bundle-age-roundtrip-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OUT)
	$(Q)bash ../scripts/tests/support_bundle_age_roundtrip_test.sh "$$PWD/$(BIN_DIR)/$(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OUT)"

support-bundle-key-provisioning-test:
	$(Q)WSPRRYPI_REAL_AGE_KEYGEN="$(AGE_KEYGEN)" python3 ../scripts/tests/support_bundle_key_provisioning_test.py

support-bundle-production-encryption-metadata-test:
	$(Q)python3 ../scripts/tests/support_bundle_production_encryption_metadata_test.py

support-bundle-signing-key-provisioning-test:
	$(Q)python3 ../scripts/tests/support_bundle_signing_key_provisioning_test.py

support-bundle-intake-manifest-preparation-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_manifest_preparation_test.py

support-bundle-intake-manifest-lifecycle-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_manifest_lifecycle_test.py

support-bundle-intake-production-manifest-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_production_manifest_test.py

support-bundle-intake-publication-commit-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_publication_commit_test.py

support-bundle-intake-publication-push-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_publication_push_test.py

support-bundle-intake-publication-verification-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_publication_verification_test.py

support-bundle-intake-trust-compilation-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_trust_compilation_test.py

support-bundle-intake-production-trust-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_production_trust_test.py

support-bundle-intake-identity-ceremony-preflight-test:
	$(Q)python3 ../scripts/tests/support_bundle_intake_identity_ceremony_preflight_test.py

support-bundle-maintainer-intake-inspection-test:
	$(Q)python3 $(SUPPORT_BUNDLE_MAINTAINER_INTAKE_INSPECTION_TEST)

support-bundle-maintainer-processing-test:
	$(Q)python3 $(SUPPORT_BUNDLE_MAINTAINER_PROCESSING_TEST)

support-bundle-retention-audit-test:
	$(Q)python3 $(SUPPORT_BUNDLE_RETENTION_AUDIT_TEST)

support-bundle-retention-deletion-test:
	$(Q)python3 $(SUPPORT_BUNDLE_RETENTION_DELETION_TEST)

support-bundle-contract-reconciliation-test:
	$(Q)python3 $(SUPPORT_BUNDLE_CONTRACT_RECONCILIATION_TEST)

support-bundle-intake-validation-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OUT)

support-bundle-intake-state-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_STATE_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_STATE_TEST_OUT)

support-bundle-intake-retrieval-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OUT) $(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OUT) "$$PWD/$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OUT)"

support-bundle-intake-controller-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OUT)

support-bundle-intake-runtime-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OUT)

support-bundle-intake-production-test: $(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OUT)

build-dependency-regression-test: $(OBJ_DIR_DEBUG)/./WSPR-Transmitter/src/transmission_controller.o
	$(Q)echo "Checking generated header dependency discovery."
	$(Q)$(MAKE) --no-print-directory -pn $< | awk 'index($$0, "$<:") == 1 && index($$0, "WSPR-Transmitter/src/execution_plan.hpp") { found = 1 } END { exit !found }'

update-check-comparison-test:
	$(Q)node tests/update_check_comparison_test.js

guarded-mode-change-persistence-test: $(BIN_DIR)/$(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OUT)
	$(Q)echo "Running guarded mode-change persistence regression test."
	$(Q)./$(BIN_DIR)/$(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OUT)

band-gpio-default-convergence-test: $(BIN_DIR)/$(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OUT)
	$(Q)echo "Running Band GPIO default convergence regression test."
	$(Q)./$(BIN_DIR)/$(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OUT)

band-gpio-rotation-test: $(BIN_DIR)/$(BAND_GPIO_ROTATION_TEST_OUT)
	$(Q)echo "Running band GPIO rotation regression tests."
	$(Q)./$(BIN_DIR)/$(BAND_GPIO_ROTATION_TEST_OUT)

selector-shutdown-cleanup-test: $(BIN_DIR)/$(SELECTOR_SHUTDOWN_CLEANUP_TEST_OUT)
	$(Q)echo "Running selector shutdown cleanup regression tests."
	$(Q)./$(BIN_DIR)/$(SELECTOR_SHUTDOWN_CLEANUP_TEST_OUT)

standard-feld-parent-integration-test: $(BIN_DIR)/$(STANDARD_FELD_PARENT_INTEGRATION_TEST_OUT)
	$(Q)echo "Running Standard Feld parent source integration tests."
	$(Q)./$(BIN_DIR)/$(STANDARD_FELD_PARENT_INTEGRATION_TEST_OUT)

standard-feld-gpio-dry-run-test:
	$(Q)$(MAKE) -C WSPR-Transmitter/src standard-feld-gpio-dry-run-test

standard-feld-rpi-execution-test:
	$(Q)$(MAKE) -C WSPR-Transmitter/src standard-feld-rpi-execution-test

startup-quiesce-parent-test: $(BIN_DIR)/startup_quiesce_parent_test
	$(Q)./$(BIN_DIR)/startup_quiesce_parent_test

qualification-gpio-test-mode-test: $(BIN_DIR)/$(QUALIFICATION_GPIO_TEST_MODE_TEST_OUT)
	$(Q)echo "Running qualification GPIO test mode value semantics test."
	$(Q)./$(BIN_DIR)/$(QUALIFICATION_GPIO_TEST_MODE_TEST_OUT)

monitor-file-regression-test: $(BIN_DIR)/$(MONITOR_FILE_REGRESSION_TEST_OUT)
	$(Q)echo "Running MonitorFile regression tests."
	$(Q)./$(BIN_DIR)/$(MONITOR_FILE_REGRESSION_TEST_OUT)

gpio-line-resolver-test: $(BIN_DIR)/$(GPIO_LINE_RESOLVER_TEST_OUT)
	$(Q)echo "Running GPIO line resolver regression tests."
	$(Q)./$(BIN_DIR)/$(GPIO_LINE_RESOLVER_TEST_OUT)

non-wspr-repeat-policy-test: $(BIN_DIR)/$(NON_WSPR_REPEAT_POLICY_TEST_OUT)
	$(Q)echo "Running non-WSPR repeat policy regression tests."
	$(Q)./$(BIN_DIR)/$(NON_WSPR_REPEAT_POLICY_TEST_OUT)

qrss-execution-regression-test qrss_execution_regression_test: $(BIN_DIR)/$(QRSS_EXECUTION_REGRESSION_TEST_OUT)
	$(Q)echo "Running QRSS/WSPR execution regression tests."
	$(Q)./$(BIN_DIR)/$(QRSS_EXECUTION_REGRESSION_TEST_OUT)

wspr-tone-regression-test wspr_tone_regression_test: $(BIN_DIR)/$(WSPR_TONE_REGRESSION_TEST_OUT)
	$(Q)echo "Running WSPR tone regression tests."
	$(Q)./$(BIN_DIR)/$(WSPR_TONE_REGRESSION_TEST_OUT)

gpio-band-policy-test: $(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT)
	$(Q)echo "Running GPIO band fail-closed policy tests."
	$(Q)./$(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_PLANNER_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_planner_test.cpp WSPR-Transmitter/src/rp1_gpclk_planner.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $^ -o $@

rp1-gpclk-planner-test: $(BIN_DIR)/$(RP1_GPCLK_PLANNER_TEST_OUT)
	$(Q)echo "Running hardware-independent RP1 GPCLK planner tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_PLANNER_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_LIFECYCLE_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_lifecycle_test.cpp WSPR-Transmitter/src/rp1_gpclk_lifecycle.cpp WSPR-Transmitter/src/rp1_gpclk_planner.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $^ -o $@

rp1-gpclk-lifecycle-test: $(BIN_DIR)/$(RP1_GPCLK_LIFECYCLE_TEST_OUT)
	$(Q)echo "Running hardware-independent RP1 GPCLK fake-driver lifecycle tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_LIFECYCLE_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_TRANSITION_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_transition_test.cpp WSPR-Transmitter/src/rp1_gpclk_transition.cpp WSPR-Transmitter/src/rp1_gpclk_planner.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) -pthread $^ -o $@

rp1-gpclk-transition-test: $(BIN_DIR)/$(RP1_GPCLK_TRANSITION_TEST_OUT)
	$(Q)echo "Running hardware-independent RP1 GPCLK transition sequencing tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_TRANSITION_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_BACKEND_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_backend_test.cpp WSPR-Transmitter/src/rp1_gpclk_backend.cpp WSPR-Transmitter/src/rp1_gpclk_planner.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $^ -o $@

rp1-gpclk-backend-test: $(BIN_DIR)/$(RP1_GPCLK_BACKEND_TEST_OUT)
	$(Q)echo "Running clock-disabled RP1 GPCLK production backend contract tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_BACKEND_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_LINUX_PROVIDER_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_linux_provider_test.cpp WSPR-Transmitter/src/rp1_gpclk_linux_provider.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $^ -o $@

rp1-gpclk-linux-provider-test: $(BIN_DIR)/$(RP1_GPCLK_LINUX_PROVIDER_TEST_OUT)
	$(Q)echo "Running address-independent RP1 GPCLK Linux provider client tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_LINUX_PROVIDER_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_transmit_backend_test.cpp WSPR-Transmitter/src/rp1_gpclk_transmit_backend.cpp WSPR-Transmitter/src/rp1_gpclk_event_program.cpp WSPR-Transmitter/src/rp1_gpclk_backend.cpp WSPR-Transmitter/src/rp1_gpclk_linux_provider.cpp WSPR-Transmitter/src/rp1_gpclk_planner.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) -pthread $^ -o $@

rp1-gpclk-transmit-backend-test: $(BIN_DIR)/$(RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT)
	$(Q)echo "Running RP1 GPCLK scheduler backend tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT)

ifeq ($(GPIOD_HAS_V2),1)
gpio-loopback-test: $(BIN_DIR)/$(GPIO_LOOPBACK_TEST_OUT)
	$(Q)echo "Built $(GPIO_LOOPBACK_TEST_OUT). Run it manually on Pi hardware."

band-gpio-live-monitor: $(BIN_DIR)/$(BAND_GPIO_LIVE_MONITOR_OUT)
	$(Q)echo "Built $(BAND_GPIO_LIVE_MONITOR_OUT). Run it manually on Pi hardware."
else
gpio-loopback-test:
	$(Q)echo "$(GPIO_LOOPBACK_TEST_OUT) requires libgpiod v2 (detected major $(GPIOD_API_MAJOR))."
	$(Q)exit 1

band-gpio-live-monitor:
	$(Q)echo "$(BAND_GPIO_LIVE_MONITOR_OUT) requires libgpiod v2 (detected major $(GPIOD_API_MAJOR))."
	$(Q)exit 1
endif

gdb: debug
	$(Q)echo "Running debug session."
	$(Q)$(SUDO) gdb --args ./$(BIN_DIR)/$(TEST_OUT) -D -i $(INI_FILE)

install-binary: release
	$(Q)echo "Installing $(OUT) binary to $(PREFIX)."
	$(Q)$(SUDO) install -d "$(PREFIX)"
	$(Q)$(SUDO) install -m 755 "$(BIN_DIR)/$(OUT)" "$(PREFIX)/$(OUT)"
	$(Q)echo "Binary installation complete: $(PREFIX)/$(OUT)"

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
	$(Q)echo "  install-binary Install only the selected release binary to $(PREFIX)"
	$(Q)echo "  install      Install the binary to $(PREFIX)"
	$(Q)echo "  debuginstall Install the debug binary to $(PREFIX)"
	$(Q)echo "  uninstall    Remove the installed binary"
	$(Q)echo "  lint         Run static analysis"
	$(Q)echo "  macros       Show macros"
	$(Q)echo "  clean        Remove build artifacts"
	$(Q)echo "  help         This message"
