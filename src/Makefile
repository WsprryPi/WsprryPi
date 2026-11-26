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

OPENSSL_CFLAGS := $(strip $(shell pkg-config --cflags openssl 2>/dev/null))
OPENSSL_LIBS   := $(strip $(shell pkg-config --libs openssl 2>/dev/null))
SQLITE_CFLAGS := $(strip $(shell pkg-config --cflags sqlite3 2>/dev/null))
SQLITE_LIBS := $(or $(strip $(shell pkg-config --libs sqlite3 2>/dev/null)),-lsqlite3)
AVAHI_AVAILABLE := $(shell pkg-config --exists avahi-client && echo 1)
AVAHI_CFLAGS := $(if $(filter Linux,$(shell uname -s 2>/dev/null)),$(if $(AVAHI_AVAILABLE),$(shell pkg-config --cflags avahi-client) -DWSPRRYPI_HAVE_AVAHI))
AVAHI_LIBS := $(if $(filter Linux,$(shell uname -s 2>/dev/null)),$(if $(AVAHI_AVAILABLE),$(shell pkg-config --libs avahi-client)))

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

# Already admitted application state for explicit manual test/debug launches.
STATE_DIRECTORY ?= /var/lib/wsprrypi/application-state

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
BUILD_PROFILE ?= transmitter
ifeq ($(filter transmitter controller,$(BUILD_PROFILE)),)
$(error Unknown BUILD_PROFILE '$(BUILD_PROFILE)'; select transmitter or controller)
endif
ifneq ($(words $(BUILD_PROFILE)),1)
$(error Select exactly one BUILD_PROFILE)
endif
ifeq ($(BUILD_PROFILE),controller)
BACKENDS ?=
ANCILLARY_GPIO ?= 0
ifneq ($(strip $(BACKENDS)),)
$(error Controller profile cannot select local BACKENDS)
endif
ifneq ($(ANCILLARY_GPIO),0)
$(error Controller profile requires ANCILLARY_GPIO=0)
endif
ifneq ($(filter command line environment override,$(origin SI5351_TRANSPORT)),)
$(error Controller profile cannot select SI5351_TRANSPORT)
endif
# These legacy recipes operate the ordinary transmitter aliases and services.
# Controller installation and lifecycle qualification are separate work.
CONTROLLER_LEGACY_GOALS := test test-tone test-oneshot gdb install install-binary debuginstall uninstall install-pairing-notices
ifneq ($(filter $(CONTROLLER_LEGACY_GOALS),$(MAKECMDGOALS)),)
$(error Controller profile does not support legacy transmitter run/install targets)
endif
else
BACKENDS ?= $(DEFAULT_BACKENDS)
ANCILLARY_GPIO ?= 1
endif
comma := ,
BACKEND_LIST := $(subst $(comma), ,$(BACKENDS))
include WSPR-Transmitter/src/si5351_transport_rules.mk
BACKEND_PROFILE_KEY := $(subst $(comma),+,$(BACKENDS))+ancillary-gpio$(ANCILLARY_GPIO)$(SI5351_TRANSPORT_KEY)
ifeq ($(BUILD_PROFILE),controller)
BACKEND_PROFILE_KEY := controller
endif

# ─────────────────────────────────────────────────────────────────────────────
# Components
# ─────────────────────────────────────────────────────────────────────────────

COMPONENT_SRCDIRS :=
COMPONENT_SRCDIRS += $(wildcard ./Chipset-Offsets/src)
COMPONENT_SRCDIRS += $(wildcard ./Mailbox/src)
COMPONENT_SRCDIRS += $(wildcard ./INI-Handler/src)
COMPONENT_SRCDIRS += $(wildcard ./LCBLog/src)
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
LEGACY_GPIO_CLOCK_MODEL_TEST_OUT := legacy_gpio_clock_model_test
LEGACY_GPIO_CLOCK_MODEL_TEST_FLAGS = $(filter-out -fmax-errors=10,$(CXX_DEBUG_FLAGS))
MAILBOX_MEMORY_FLAG_TEST_OUT := mailbox_memory_flag_test
MAILBOX_MEMORY_FLAG_TEST_FLAGS = $(filter-out -fmax-errors=10,$(CXX_DEBUG_FLAGS))
GPIO_FREQUENCY_CORRECTION_TEST_OUT := gpio_frequency_correction_test
GPIO_FREQUENCY_CORRECTION_TEST_FLAGS = $(filter-out -fmax-errors=10,$(CXX_DEBUG_FLAGS))
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
RP1_GPCLK_DEVELOPMENT_POLICY_TEST_OUT := rp1_gpclk_development_policy_test
RP1_GPCLK_ROUTE_RUNTIME_WIRING_TEST_OUT := rp1_gpclk_route_runtime_wiring_test
RP1_GPCLK_ROUTE_SERVICE_TEST_OUT := rp1_gpclk_route_service_test
RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT := rp1_gpclk_transmit_backend_test
RP1_GPCLK_ADMIN_PROBE_OUT := rp1_gpclk_admin_probe
SELECTOR_SHUTDOWN_CLEANUP_TEST_OUT := selector_shutdown_cleanup_test
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
CONTROLLER_PRODUCTION_FIXTURE := $(PROFILE_BIN_DIR)/controller_only_production_fixture
PROFILE_LINK_INPUTS := Makefile
ifeq ($(BUILD_PROFILE),controller)
PROFILE_LINK_INPUTS += ../scripts/controller_sources.mk
ifeq ($(HOST_OS),Darwin)
CONTROLLER_LINK_MAP = -Wl,-map,$@.map
else
CONTROLLER_LINK_MAP = -Wl,-Map,$@.map
endif
endif
BUILD_METADATA_DIR := build/generated
ifeq ($(BUILD_PROFILE),controller)
BUILD_METADATA_DIR := build/generated/controller
endif
BUILD_METADATA_HEADER := $(BUILD_METADATA_DIR)/build_metadata.hpp
BACKEND_CAPABILITIES_HEADER := $(BUILD_METADATA_DIR)/backend_capabilities.hpp
BACKEND_CAPABILITIES_GENERATOR := ../scripts/generate_backend_capabilities.py
BACKEND_CAPABILITIES_REGRESSION_TEST := ../scripts/tests/backend_capabilities_regression_test.py
BACKEND_CAPABILITIES_MAKE_REGRESSION_TEST := ../scripts/tests/backend_capabilities_make_regression_test.py
SEMANTICS_MAKE_REGRESSION_TEST := ../scripts/tests/semantics_make_regression_test.py
BUILD_METADATA_GENERATOR := ../scripts/generate_build_metadata.py
BUILD_METADATA_REGRESSION_TEST := ../scripts/tests/build_metadata_regression_test.py
BUILD_METADATA_MAKE_REGRESSION_TEST := ../scripts/tests/build_metadata_make_regression_test.py
SUPPORT_BUNDLE_MAINTAINER_INTAKE_INSPECTION_TEST := ../scripts/tests/support_bundle_maintainer_intake_inspection_test.py
SUPPORT_BUNDLE_MAINTAINER_PROCESSING_TEST := ../scripts/tests/support_bundle_maintainer_processing_test.py
SUPPORT_BUNDLE_RETENTION_AUDIT_TEST := ../scripts/tests/support_bundle_retention_audit_test.py
SUPPORT_BUNDLE_RETENTION_DELETION_TEST := ../scripts/tests/support_bundle_retention_deletion_test.py
SUPPORT_BUNDLE_COLLECTOR_CONTRACT_TEST := ../scripts/tests/collect-support-bundle_test.sh
BUILD_METADATA_REPO_ROOT := ..
BUILD_METADATA_PROJECT := $(PRJ)
BUILD_METADATA_EXECUTABLE := $(OUT)
VERSION_RELEASE_OBJECT := $(OBJ_DIR_RELEASE)/./version.o
VERSION_DEBUG_OBJECT := $(OBJ_DIR_DEBUG)/./version.o

.DEFAULT_GOAL := release

# Parent-owned application storage and focused configuration checks.
PRIVATE_ACL_FOCUSED_TESTS := sqlite_state_store_test persistent_configuration_test controller_settings_test sqlite_catalog_test companion_files_test state_migration_test controller_identity_store_test recurrence_progress_test recovery_destination_settings_test recovery_evidence_test
$(addprefix $(BIN_DIR)/,$(PRIVATE_ACL_FOCUSED_TESTS)): native_private_acl.hpp
$(BIN_DIR)/sqlite_state_store_test $(BIN_DIR)/companion_files_test $(BIN_DIR)/controller_identity_store_test: tests/software_private_acl.hpp

.PHONY: sqlite-state-store-test
sqlite-state-store-test: $(BIN_DIR)/sqlite_state_store_test
	$(Q)./$(BIN_DIR)/sqlite_state_store_test

$(BIN_DIR)/sqlite_state_store_test: tests/sqlite_state_store_test.cpp tests/software_child_process.hpp application_storage/sqlite_state_store.cpp application_storage/sqlite_state_store.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG $(SQLITE_CFLAGS) -I. tests/sqlite_state_store_test.cpp application_storage/sqlite_state_store.cpp $(SQLITE_LIBS) -o $@

.PHONY: persistent-configuration-test
persistent-configuration-test: $(BIN_DIR)/persistent_configuration_test
	$(Q)./$(BIN_DIR)/persistent_configuration_test wtp_integration/contract/control-controller-vectors.json

$(BIN_DIR)/persistent_configuration_test: tests/persistent_configuration_test.cpp application_storage/persistent_configuration.cpp application_storage/persistent_configuration.hpp application_storage/sqlite_state_store.cpp application_storage/sqlite_state_store.hpp wtp_integration/controller_contract.cpp wtp_integration/control_contract.hpp wtp_integration/controller_contract.hpp wtp_integration/browser_api.cpp wtp_integration/browser_api.hpp wtp_integration/network_http.hpp json.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG $(SQLITE_CFLAGS) -I. tests/persistent_configuration_test.cpp application_storage/persistent_configuration.cpp application_storage/sqlite_state_store.cpp wtp_integration/controller_contract.cpp wtp_integration/browser_api.cpp $(SQLITE_LIBS) -o $@

.PHONY: sqlite-catalog-test
.PHONY: controller-settings-test
controller-settings-test: $(BIN_DIR)/controller_settings_test
	$(Q)./$(BIN_DIR)/controller_settings_test

$(BIN_DIR)/controller_settings_test: tests/controller_settings_test.cpp application_storage/controller_settings.cpp application_storage/controller_settings.hpp application_control/advanced_settings.cpp application_control/advanced_settings.hpp application_storage/persistent_configuration.cpp application_storage/sqlite_state_store.cpp wtp_integration/controller_contract.cpp wtp_integration/browser_api.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG $(SQLITE_CFLAGS) -I. $(filter %.cpp,$^) $(SQLITE_LIBS) -o $@

sqlite-catalog-test: $(BIN_DIR)/sqlite_catalog_test
	$(Q)./$(BIN_DIR)/sqlite_catalog_test

$(BIN_DIR)/sqlite_catalog_test: tests/sqlite_catalog_test.cpp application_storage/sqlite_catalog.cpp application_storage/sqlite_catalog.hpp application_storage/sqlite_state_store.cpp application_storage/sqlite_state_store.hpp wtp_integration/catalog.cpp wtp_integration/catalog.hpp wtp_settings_json.hpp wtp_settings.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG $(SQLITE_CFLAGS) $(OPENSSL_CFLAGS) -I. tests/sqlite_catalog_test.cpp application_storage/sqlite_catalog.cpp application_storage/sqlite_state_store.cpp wtp_integration/catalog.cpp $(SQLITE_LIBS) $(OPENSSL_LIBS) -o $@

.PHONY: companion-files-test
companion-files-test: $(BIN_DIR)/companion_files_test
	$(Q)./$(BIN_DIR)/companion_files_test

$(BIN_DIR)/companion_files_test: tests/companion_files_test.cpp application_storage/companion_files.cpp application_storage/companion_files.hpp application_storage/sqlite_state_store.cpp application_storage/sqlite_state_store.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG $(SQLITE_CFLAGS) $(OPENSSL_CFLAGS) -I. $(filter %.cpp,$^) $(SQLITE_LIBS) $(OPENSSL_LIBS) -o $@

.PHONY: state-migration-test
state-migration-test: $(BIN_DIR)/state_migration_test
	$(Q)./$(BIN_DIR)/state_migration_test

$(BIN_DIR)/state_migration_test: tests/state_migration_test.cpp tests/software_child_process.hpp application_storage/state_migration.cpp application_storage/state_migration.hpp application_storage/generation_selection.cpp application_storage/generation_selection.hpp application_storage/companion_files.cpp application_storage/companion_files.hpp application_storage/sqlite_state_store.cpp application_storage/sqlite_state_store.hpp application_storage/persistent_configuration.cpp application_storage/sqlite_catalog.cpp owned_state_directory.cpp wtp_integration/pi_management_journal.cpp wtp_integration/catalog.cpp wtp_integration/controller_contract.cpp wtp_integration/browser_api.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG $(SQLITE_CFLAGS) $(OPENSSL_CFLAGS) -I. $(filter %.cpp,$^) $(SQLITE_LIBS) $(OPENSSL_LIBS) -o $@

include ../scripts/build_metadata_rules.mk
include ../scripts/backend_capability_rules.mk

HOST_OS := $(shell uname -s 2>/dev/null)
NATIVE_USB_LIBS := $(if $(filter Darwin,$(HOST_OS)),-framework IOKit -framework CoreFoundation)
ifeq ($(OS),Windows_NT)
NATIVE_USB_LIBS := -lsetupapi -lcfgmgr32 -ladvapi32
NATIVE_HOST_LIBS := -lws2_32 -lbcrypt -ladvapi32 -lshell32 -lole32 -luuid -liphlpapi -lcrypt32 -lmincore
endif
NATIVE_CDC_SOURCES := wtp_integration/usb_cdc_posix.cpp wtp_integration/usb_cdc_windows_io.cpp wtp_integration/usb_cdc_windows.cpp wtp_integration/usb_cdc_native.cpp wtp_integration/usb_cdc_macos.cpp wtp_integration/usb_cdc_descriptors.cpp

# ─────────────────────────────────────────────────────────────────────────────
# Source discovery (C++ only)
# ─────────────────────────────────────────────────────────────────────────────

LOCAL_CPP_SOURCES := $(shell find . -maxdepth 1 -type f -name '*.cpp' ! -name '._*' 2>/dev/null)
CONTROLLER_RUNTIME_ADAPTER_SOURCES := ./controller_local_hardware_unavailable.cpp ./transmitter_runtime_bridge_controller.cpp ./arg_parser_runtime_controller.cpp
LOCAL_CPP_SOURCES := $(filter-out $(CONTROLLER_RUNTIME_ADAPTER_SOURCES),$(LOCAL_CPP_SOURCES))
ifeq ($(BUILD_PROFILE),controller)
LOCAL_CPP_SOURCES := $(filter-out ./transmitter_runtime_bridge.cpp ./arg_parser_runtime.cpp,$(LOCAL_CPP_SOURCES)) $(CONTROLLER_RUNTIME_ADAPTER_SOURCES)
endif
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
LOCAL_CPP_SOURCES += ./udp_logging/host.cpp
ALL_SI5351_TRANSPORT_SOURCES := ./WSPR-Transmitter/src/si5351_transport_linux.cpp ./WSPR-Transmitter/src/si5351_transport_macos.cpp
COMPONENT_CPP_SOURCES := $(shell find $(COMPONENT_SRCDIRS) -type f -name '*.cpp' ! -name '._*' ! -name 'main.cpp' 2>/dev/null)
COMPONENT_CPP_SOURCES := $(filter-out $(ALL_SI5351_TRANSPORT_SOURCES) \
    ./WSPR-Transmitter/src/bcm2835_qualification_plan.cpp \
    ./WSPR-Transmitter/src/bcm2835_qualification_hardware.cpp,$(COMPONENT_CPP_SOURCES))
TRANSMITTER_SHARED_CPP_SOURCES := \
	./WSPR-Transmitter/src/execution_plan_compiler.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_development_policy.cpp \
	./WSPR-Transmitter/src/transmission_controller.cpp \
	./WSPR-Transmitter/src/wspr_reference_adapter.cpp \
	./WSPR-Transmitter/src/wspr_transmit.cpp
RPI_GPIO_BACKEND_CPP_SOURCES := \
	./Mailbox/src/mailbox.cpp \
	./WSPR-Transmitter/src/gpio_frequency_plan.cpp \
	./WSPR-Transmitter/src/wspr_transmit_backend_rpi.cpp
RP1_GPCLK_BACKEND_CPP_SOURCES := \
	./WSPR-Transmitter/src/rp1_gpclk_backend.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_event_program.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_development_policy.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_lifecycle.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_linux_provider.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_planner.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_reconciliation.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_transition.cpp \
	./WSPR-Transmitter/src/rp1_gpclk_transmit_backend.cpp
SI5351_BACKEND_CPP_SOURCES := \
	./WSPR-Transmitter/src/si5351_device.cpp \
	$(addprefix ./WSPR-Transmitter/src/,$(SI5351_TRANSPORT_SOURCE)) \
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
SIGNAL_HANDLER_WINDOWS_TEST_SOURCE := ./Signal-Handler/src/test_windows.cpp
SINGLETON_TEST_SOURCE := ./Singleton/src/test.cpp ./Singleton/src/installation_lock_test.cpp
SI5351_TEST_SOURCE := ./WSPR-Transmitter/src/si5351_backend_harness.cpp
SI5351_PLANNER_TEST_SOURCE := ./WSPR-Transmitter/src/si5351_planner_test.cpp
SI5351_TRANSITION_TEST_SOURCE := ./WSPR-Transmitter/src/si5351_transition_test.cpp
STARTUP_QUIESCE_TEST_SOURCE := ./WSPR-Transmitter/src/startup_quiesce_test.cpp
LEGACY_GPIO_CLOCK_MODEL_TEST_SOURCE := ./WSPR-Transmitter/src/legacy_gpio_clock_model_test.cpp
GPIO_FREQUENCY_CORRECTION_TEST_SOURCE := ./tests/gpio_frequency_correction_test.cpp
SI5351_QUALIFICATION_SOURCE := ./WSPR-Transmitter/src/si5351_startup_quiesce_qualification.cpp
SI5351_QUALIFICATION_MAIN_SOURCE := ./WSPR-Transmitter/src/si5351_startup_quiesce_qualification_main.cpp
SI5351_QUALIFICATION_TEST_SOURCE := ./WSPR-Transmitter/src/si5351_startup_quiesce_qualification_test.cpp
RP1_GPCLK_PLANNER_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_planner_test.cpp
RP1_GPCLK_LIFECYCLE_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_lifecycle_test.cpp
RP1_GPCLK_TRANSITION_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_transition_test.cpp
RP1_GPCLK_BACKEND_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_backend_test.cpp
RP1_GPCLK_LINUX_PROVIDER_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_linux_provider_test.cpp
RP1_GPCLK_DEVELOPMENT_POLICY_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_development_policy_test.cpp
RP1_GPCLK_TRANSMIT_BACKEND_TEST_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_transmit_backend_test.cpp
RP1_GPCLK_ADMIN_PROBE_SOURCE := ./WSPR-Transmitter/src/rp1_gpclk_admin_probe.cpp
SIMULATED_BACKEND_TEST_SOURCE := ./WSPR-Transmitter/src/simulated_transmit_backend_test.cpp
SIMULATED_BACKEND_REALTIME_TEST_SOURCE := ./WSPR-Transmitter/src/simulated_transmit_backend_realtime_test.cpp
TRANSMISSION_CONTROLLER_CONTRACT_TEST_SOURCE := ./WSPR-Transmitter/src/transmission_controller_contract_test.cpp
THREAD_AFFINITY_UNAVAILABLE_TEST_SOURCE := ./WSPR-Transmitter/src/thread_affinity_unavailable_test.cpp
COMPONENT_CPP_SOURCES := $(filter-out $(PPM_MANAGER_TEST_SOURCE) $(SIGNAL_HANDLER_TEST_SOURCE) $(SIGNAL_HANDLER_WINDOWS_TEST_SOURCE) $(SINGLETON_TEST_SOURCE) ./MonitorFile/src/monitorfile.cpp $(SI5351_TEST_SOURCE) $(SI5351_PLANNER_TEST_SOURCE) $(SI5351_TRANSITION_TEST_SOURCE) $(STARTUP_QUIESCE_TEST_SOURCE) $(LEGACY_GPIO_CLOCK_MODEL_TEST_SOURCE) $(GPIO_FREQUENCY_CORRECTION_TEST_SOURCE) $(SI5351_QUALIFICATION_SOURCE) $(SI5351_QUALIFICATION_MAIN_SOURCE) $(SI5351_QUALIFICATION_TEST_SOURCE) $(RP1_GPCLK_PLANNER_TEST_SOURCE) $(RP1_GPCLK_LIFECYCLE_TEST_SOURCE) $(RP1_GPCLK_TRANSITION_TEST_SOURCE) $(RP1_GPCLK_BACKEND_TEST_SOURCE) $(RP1_GPCLK_LINUX_PROVIDER_TEST_SOURCE) $(RP1_GPCLK_DEVELOPMENT_POLICY_TEST_SOURCE) $(RP1_GPCLK_TRANSMIT_BACKEND_TEST_SOURCE) $(RP1_GPCLK_ADMIN_PROBE_SOURCE) $(SIMULATED_BACKEND_TEST_SOURCE) $(SIMULATED_BACKEND_REALTIME_TEST_SOURCE) $(TRANSMISSION_CONTROLLER_CONTRACT_TEST_SOURCE) $(THREAD_AFFINITY_UNAVAILABLE_TEST_SOURCE),$(COMPONENT_CPP_SOURCES))
COMPONENT_CPP_SOURCES := $(filter-out $(TRANSMITTER_SHARED_CPP_SOURCES) $(TRANSMITTER_BACKEND_CPP_SOURCES),$(COMPONENT_CPP_SOURCES))

WTP_APPLICATION_SOURCES := $(wildcard wtp_integration/*.cpp)
WTP_APPLICATION_SOURCES += $(wildcard WTP-Server/src/*.cpp)
WTP_APPLICATION_SOURCES += $(filter-out wtp_endpoint/si5351_bridge.cpp wtp_endpoint/si5351_realization.cpp,$(wildcard wtp_endpoint/*.cpp))
WTP_APPLICATION_SOURCES += $(if $(filter si5351,$(BACKEND_LIST)),wtp_endpoint/si5351_bridge.cpp wtp_endpoint/si5351_realization.cpp)
CPP_SOURCES := $(WTP_APPLICATION_SOURCES) $(LOCAL_CPP_SOURCES) $(wildcard application_storage/*.cpp) $(wildcard application_control/*.cpp) $(wildcard application_recovery/*.cpp) $(COMPONENT_CPP_SOURCES) $(TRANSMITTER_SHARED_CPP_SOURCES) $(SELECTED_BACKEND_CPP_SOURCES)
ifeq ($(BUILD_PROFILE),controller)
include ../scripts/controller_sources.mk
CPP_SOURCES := $(CONTROLLER_CPP_SOURCES)
endif
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
WEB_SERVER_NETWORK_LIFECYCLE_TEST_SRC := tests/web_server_network_lifecycle_test.cpp
WEB_SERVER_NETWORK_LIFECYCLE_TEST_OBJ := $(OBJ_DIR_DEBUG)/tests/web_server_network_lifecycle_test.o
WEB_SERVER_NETWORK_LIFECYCLE_TEST_DEPS := $(WEB_SERVER_NETWORK_LIFECYCLE_TEST_OBJ) $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
WEB_SERVER_NETWORK_LIFECYCLE_TEST_OUT := web_server_network_lifecycle_test
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
SUPPORT_REQUEST_GUARD_TEST_DEPS := $(SUPPORT_REQUEST_GUARD_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_request_guard.o $(OBJ_DIR_DEBUG)/./privileged_network_policy.o $(OBJ_DIR_DEBUG)/./native_windows_network.o
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
SUPPORT_BUNDLE_HTTP_TEST_DEPS := $(SUPPORT_BUNDLE_HTTP_TEST_OBJ) $(OBJ_DIR_DEBUG)/./support_bundle_http.o $(OBJ_DIR_DEBUG)/./support_bundle_http_common.o $(OBJ_DIR_DEBUG)/./support_bundle_http_intake_routes.o $(OBJ_DIR_DEBUG)/./support_bundle_http_job_routes.o $(OBJ_DIR_DEBUG)/./support_bundle_http_download_routes.o $(OBJ_DIR_DEBUG)/./support_bundle_http_query_routes.o $(OBJ_DIR_DEBUG)/./support_bundle_download_preparation.o $(OBJ_DIR_DEBUG)/./support_bundle_archive_digest.o $(OBJ_DIR_DEBUG)/./support_bundle_checksum_file.o $(OBJ_DIR_DEBUG)/./support_bundle_download_file.o $(OBJ_DIR_DEBUG)/./support_bundle_job_manager.o $(OBJ_DIR_DEBUG)/./support_bundle_result_validator.o $(OBJ_DIR_DEBUG)/./support_bundle_job_directory_remover.o $(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o $(OBJ_DIR_DEBUG)/./support_request_guard.o $(OBJ_DIR_DEBUG)/./privileged_network_policy.o $(OBJ_DIR_DEBUG)/./httplib.o
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

# Keep dependency files beside each object, isolated by debug/release and backend
# profile. A shared .d file can otherwise hide a header ABI change from one build.
DEPENDENCY_OBJECTS := $(sort $(CPP_OBJECTS) $(CPP_DEBUG_OBJECTS) $(filter %.o,$(foreach variable,$(filter %_OBJ %_DEBUG_OBJ,$(.VARIABLES)),$($(variable)))))
DEPFILES := $(wildcard $(DEPENDENCY_OBJECTS:.o=.d))
-include $(DEPFILES)
# Migrate an existing object which has only a legacy shared dependency file.
MISSING_DEP_OBJECTS := $(foreach object,$(DEPENDENCY_OBJECTS),$(if $(wildcard $(object:.o=.d)),,$(object)))
.PHONY: FORCE_OBJECT_DEPENDENCIES
FORCE_OBJECT_DEPENDENCIES:
$(MISSING_DEP_OBJECTS): FORCE_OBJECT_DEPENDENCIES

# ─────────────────────────────────────────────────────────────────────────────
# Toolchain
# ─────────────────────────────────────────────────────────────────────────────

CXX := g++

# The full semantics suite intentionally compiles Linux GPIO integration even
# though it does not operate hardware. Detect its C++ header up front so an
# unsupported host receives an actionable error instead of a late compiler
# failure. Tests may override this probe without changing the selected backend.
GPIOD_CPP_HEADER_AVAILABLE ?= $(shell printf '%s\n' '\#include <gpiod.hpp>' | $(CXX) -x c++ -E - >/dev/null 2>&1 && echo 1 || echo 0)

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

# Select a budget only at an explicit caller boundary; ordinary builds leave GNU Make's
# explicit/inherited jobserver intact and use its standard serial default.
ifneq ($(filter ci-build-budget build-job-count,$(MAKECMDGOALS)),)
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

# Bound automatic sizing even on hosts exposing many CPUs. Existing low-memory
# limits still apply; explicit JOBS or command-line -j remain caller decisions.
ifneq ($(filter ci-build-budget,$(MAKECMDGOALS)),)
BUILD_JOB_LIMIT ?= 4
ifneq ($(words $(BUILD_JOB_LIMIT)),1)
$(error BUILD_JOB_LIMIT must be one integer from 1 through 4)
endif
ifneq ($(filter 1 2 3 4,$(BUILD_JOB_LIMIT)),$(BUILD_JOB_LIMIT))
$(error BUILD_JOB_LIMIT must be one integer from 1 through 4)
endif
JOBS_COMPUTED := $(shell j=$(JOBS_COMPUTED); if [ $$j -gt $(BUILD_JOB_LIMIT) ]; then echo $(BUILD_JOB_LIMIT); else echo $$j; fi)
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

endif

.PHONY: ci-build-budget build-job-count
build-job-count:
	$(Q)case $(call shell_quote,$(JOBS)) in ''|*[!0-9]*|0) echo "stage=build-budget; expected=positive finite job count" >&2; exit 1;; esac; \
		printf '%s\n' $(call shell_quote,$(JOBS))

ci-build-budget:
	$(Q)test -n "$$GITHUB_ENV" || { echo "stage=build-budget; GITHUB_ENV is required" >&2; exit 1; }
	$(Q)set -e; case $(call shell_quote,$(JOBS)) in 1|2|3|4) ;; *) echo "stage=build-budget; jobs must be 1 through 4" >&2; exit 1;; esac; \
		printf 'MAKEFLAGS=-j%s\n' $(call shell_quote,$(JOBS)) >> "$$GITHUB_ENV"; \
		printf 'CI build jobs: %s; processors: %s; available KiB: %s\n' $(call shell_quote,$(JOBS)) $(call shell_quote,$(NPROCS)) $(call shell_quote,$(MEM_AVAIL_KB))

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
COMPILER_DIAGNOSTIC_FLAGS := -ferror-limit=10
else
COMPILER_DIAGNOSTIC_FLAGS := -fmax-errors=10
endif

COMMON_FLAGS := -Wall -Werror $(COMPILER_DIAGNOSTIC_FLAGS) -MMD -MP

# Base compiler flags
CXXFLAGS := $(SI5351_TRANSPORT_FLAGS) $(if $(findstring clang,$(CXX_VERSION_LINE)),,-Wno-psabi) -std=c++$(CXXVER) \
           $(COMMON_FLAGS) \
           $(PKG_CFLAGS) \
           $(OPENSSL_CFLAGS) \
           $(SQLITE_CFLAGS) \
           $(AVAHI_CFLAGS) \
           $(SYSTEMD_CFLAGS) \
           -DGPIOD_API_MAJOR=$(GPIOD_API_MAJOR) \
           -DLIB_JOURNALD=$(HAVE_JOURNALD)

# Include paths
CXXFLAGS += -I$(abspath ./WTP-Client/include)
CXXFLAGS += -I$(abspath ./WTP-Server/include)
CXXFLAGS += -I$(abspath .)
CXXFLAGS += -I$(abspath $(BUILD_METADATA_DIR))
CXXFLAGS += $(foreach dir,$(COMPONENT_SRCDIRS),-I$(abspath $(dir)))
CXXFLAGS += -I$(abspath ./WSPR-Reference/src/wspr)
ifeq ($(OS),Windows_NT)
CXXFLAGS += -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DNOGDI
endif

# Debug/release variants
CXX_DEBUG_FLAGS   := $(CXXFLAGS) -g $(DEBUG_DEFS)
CXX_RELEASE_FLAGS := $(CXXFLAGS) -O2

# Linker flags
ifeq ($(HOST_OS),Linux)
ATOMIC_LIB := -latomic
else
ATOMIC_LIB :=
endif

LDFLAGS = $(SI5351_TRANSPORT_LIBS) -lpthread $(ATOMIC_LIB) \
           $(NATIVE_USB_LIBS) \
           $(NATIVE_HOST_LIBS) \
           $(SQLITE_LIBS) \
           $(PKG_LIBS) \
           $(OPENSSL_LIBS) \
           $(AVAHI_LIBS) \
           $(SYSTEMD_LIBS) $(PAIRING_PROVIDER_LINK)

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
.PHONY: all
all: release

# The PAKE candidate is isolated from system installations and existing TLS.
# Ordinary application builds do not enable pairing at this qualification stage.
PAIRING_PROVIDER_KEY := $(shell python3 ../scripts/build_pairing_provider.py --key --compiler "$(CXX)")
PAIRING_PROVIDER_ROOT := $(abspath build/pairing-provider/$(PAIRING_PROVIDER_KEY))
PAIRING_PROVIDER_CACHE ?= $(abspath build/pairing-provider-cache)
PAIRING_PROVIDER_LIB := $(PAIRING_PROVIDER_ROOT)/sdk/usr/lib/libbotan-3.a
PAIRING_PROVIDER_INCLUDE := $(PAIRING_PROVIDER_ROOT)/sdk/usr/include/botan-3
PAIRING_PROVIDER_LINK = $(if $(filter %/pi_pair_crypto.o %/pi_pair_crypto.cpp,$^),$(PAIRING_PROVIDER_LIB))
CXX_DEBUG_FLAGS += -I"$(PAIRING_PROVIDER_INCLUDE)"
CXX_RELEASE_FLAGS += -I"$(PAIRING_PROVIDER_INCLUDE)"
$(filter %/pi_pair_crypto.o,$(CPP_OBJECTS) $(CPP_DEBUG_OBJECTS)): $(PAIRING_PROVIDER_LIB)

$(PAIRING_PROVIDER_LIB): ../scripts/build_pairing_provider.py ../scripts/pairing_provider.json
	$(Q)python3 ../scripts/build_pairing_provider.py --root "$(PAIRING_PROVIDER_ROOT)" --cache "$(PAIRING_PROVIDER_CACHE)" --compiler "$(CXX)"

.PHONY: pairing-provider-test
pairing-provider-test: $(PAIRING_PROVIDER_LIB)
	$(Q)python3 ../scripts/tests/pairing_provider_build_test.py
	$(Q)$(CXX) -std=c++20 -O2 -Wall -Wextra -Werror -DPROVIDER_WITH_OPENSSL $(OPENSSL_CFLAGS) -I"$(PAIRING_PROVIDER_INCLUDE)" tests/pairing_provider/profile_probe.cpp "$(PAIRING_PROVIDER_LIB)" $(OPENSSL_LIBS) -o "$(PAIRING_PROVIDER_ROOT)/profile-probe"
	$(Q)"$(PAIRING_PROVIDER_ROOT)/profile-probe"

.PHONY: control-contract-check
control-contract-check:
	$(Q)python3 ../scripts/generate_control_contract.py --check
	$(Q)python3 ../scripts/generate_pair_contract.py --check

# One prerequisite per make invocation; optimized release remains a candidate
# build. Stable publication policy is selected only by an explicit stable tag.
$(CPP_OBJECTS) $(CPP_DEBUG_OBJECTS) $(PROFILE_RELEASE_BIN) $(PROFILE_DEBUG_BIN): | control-contract-check


$(OBJ_DIR_DEBUG)/%.o: %.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

define MAKE_DEBUG_COMPONENT_RULES
$(OBJ_DIR_DEBUG)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.cpp
	$(Q)mkdir -p $$(@D)
	$(Q)echo "Compiling (debug) $$< into $$@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $$(@:.o=.d) -c $$< -o $$@
endef
$(foreach dir,$(COMPONENT_SRCDIRS),$(eval $(call MAKE_DEBUG_COMPONENT_RULES,$(dir))))

# Link debug binary
$(PROFILE_DEBUG_BIN): $(CPP_DEBUG_OBJECTS) $(PROFILE_LINK_INPUTS)
	$(Q)mkdir -p $(PROFILE_BIN_DIR)
	$(Q)echo "Linking debug: $(TEST_OUT) (libgpiod major: $(GPIOD_API_MAJOR))"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $(filter %.o,$^) -o $@ $(LDFLAGS) $(CONTROLLER_LINK_MAP)

.PHONY: FORCE_PROFILE_BINARY
FORCE_PROFILE_BINARY:

ifneq ($(BUILD_PROFILE),controller)
$(BIN_DIR)/$(TEST_OUT): $(PROFILE_DEBUG_BIN) FORCE_PROFILE_BINARY
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)cp $(PROFILE_DEBUG_BIN) $@
endif

$(SEMANTICS_TEST_OBJ): $(SEMANTICS_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SEMANTICS_TEST_OUT): $(SEMANTICS_TEST_OBJ) $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(SEMANTICS_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(CLEANUP_LIFECYCLE_TEST_OBJ): $(CLEANUP_LIFECYCLE_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(CLEANUP_LIFECYCLE_TEST_OUT): $(CLEANUP_LIFECYCLE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(CLEANUP_LIFECYCLE_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(UI_SOURCE_REGRESSION_TEST_OBJ): $(UI_SOURCE_REGRESSION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(UI_SOURCE_REGRESSION_TEST_OUT): $(UI_SOURCE_REGRESSION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(UI_SOURCE_REGRESSION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(WEBSOCKET_LIFECYCLE_TEST_OUT): $(WEBSOCKET_LIFECYCLE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(WEB_SERVER_NETWORK_LIFECYCLE_TEST_OUT): $(WEB_SERVER_NETWORK_LIFECYCLE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(WSPR_BAND_CATALOG_TEST_OBJ): $(WSPR_BAND_CATALOG_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

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
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OUT): $(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OBJ) $(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(GUARDED_MODE_CHANGE_PERSISTENCE_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OBJ): $(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OUT): $(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OBJ) $(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(BAND_GPIO_DEFAULT_CONVERGENCE_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BAND_GPIO_ROTATION_TEST_OBJ): $(BAND_GPIO_ROTATION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(BAND_GPIO_ROTATION_TEST_OUT): $(BAND_GPIO_ROTATION_TEST_OBJ) $(BAND_GPIO_ROTATION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(BAND_GPIO_ROTATION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(MONITOR_FILE_REGRESSION_TEST_OBJ): $(MONITOR_FILE_REGRESSION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -IMonitorFile/src -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(MONITOR_FILE_REGRESSION_TEST_OUT): $(MONITOR_FILE_REGRESSION_TEST_OBJ) $(MONITOR_FILE_REGRESSION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(MONITOR_FILE_REGRESSION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(GPIO_LINE_RESOLVER_TEST_OBJ): $(GPIO_LINE_RESOLVER_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(GPIO_LINE_RESOLVER_TEST_OUT): $(GPIO_LINE_RESOLVER_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(GPIO_LINE_RESOLVER_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(NON_WSPR_REPEAT_POLICY_TEST_OBJ): $(NON_WSPR_REPEAT_POLICY_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(NON_WSPR_REPEAT_POLICY_TEST_OUT): $(NON_WSPR_REPEAT_POLICY_TEST_OBJ) $(NON_WSPR_REPEAT_POLICY_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(NON_WSPR_REPEAT_POLICY_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(QRSS_EXECUTION_REGRESSION_TEST_OBJ): $(QRSS_EXECUTION_REGRESSION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(QRSS_EXECUTION_REGRESSION_TEST_OUT): $(QRSS_EXECUTION_REGRESSION_TEST_OBJ) $(QRSS_EXECUTION_REGRESSION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(QRSS_EXECUTION_REGRESSION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(WSPR_TONE_REGRESSION_TEST_OBJ): $(WSPR_TONE_REGRESSION_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(WSPR_TONE_REGRESSION_TEST_OUT): $(WSPR_TONE_REGRESSION_TEST_OBJ) $(WSPR_TONE_REGRESSION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(WSPR_TONE_REGRESSION_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(GPIO_BAND_POLICY_TEST_OBJ): $(GPIO_BAND_POLICY_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT): $(GPIO_BAND_POLICY_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(GPIO_BAND_POLICY_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(SELECTOR_SHUTDOWN_CLEANUP_TEST_OBJ): $(SELECTOR_SHUTDOWN_CLEANUP_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SELECTOR_SHUTDOWN_CLEANUP_TEST_OUT): $(SELECTOR_SHUTDOWN_CLEANUP_TEST_OBJ) $(SELECTOR_SHUTDOWN_CLEANUP_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(SELECTOR_SHUTDOWN_CLEANUP_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(STARTUP_QUIESCE_PARENT_TEST_OBJ): $(STARTUP_QUIESCE_PARENT_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/startup_quiesce_parent_test: $(STARTUP_QUIESCE_PARENT_TEST_OBJ) $(STARTUP_QUIESCE_PARENT_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BACKEND_PROFILE_FACTORY_TEST_OBJ): $(BACKEND_PROFILE_FACTORY_TEST_SRC) $(BACKEND_CAPABILITIES_HEADER)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BACKEND_PROFILE_FACTORY_TEST_BIN): $(BACKEND_PROFILE_FACTORY_TEST_DEPS)
	$(Q)mkdir -p $(PROFILE_BIN_DIR)
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) $^ -o $@ $(LDFLAGS)

$(QUALIFICATION_GPIO_TEST_MODE_TEST_OBJ): $(QUALIFICATION_GPIO_TEST_MODE_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

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
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_private_artifact.o: support_bundle_private_artifact.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_OUT): $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OBJ): $(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_OUT): $(SUPPORT_BUNDLE_AGE_ROUNDTRIP_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_PRIVATE_ARTIFACT_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_validation.o: support_bundle_intake_validation.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_STATE_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_STATE_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_state.o: support_bundle_intake_state.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_STATE_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_STATE_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OBJ): $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_retrieval.o: support_bundle_intake_retrieval.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OUT): $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_HELPER_OBJ)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_RETRIEVAL_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_controller.o: support_bundle_intake_controller.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_CONTROLLER_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_runtime.o: support_bundle_intake_runtime.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(OBJ_DIR_DEBUG)/./support_bundle_intake_production.o: support_bundle_intake_production.cpp
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/.
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_RUNTIME_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

$(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OBJ): $(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_SRC)
	$(Q)mkdir -p $(dir $@) $(DEP_DIR)/tests
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_OUT): $(SUPPORT_BUNDLE_INTAKE_PRODUCTION_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_FLAGS) $^ -o $@ $(SUPPORT_BUNDLE_INTAKE_VALIDATION_TEST_LDFLAGS)

ifeq ($(GPIOD_HAS_V2),1)
$(GPIO_LOOPBACK_TEST_OBJ): $(GPIO_LOOPBACK_TEST_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/$(GPIO_LOOPBACK_TEST_OUT): $(GPIO_LOOPBACK_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)echo "Linking debug: $(GPIO_LOOPBACK_TEST_OUT)"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

$(BAND_GPIO_LIVE_MONITOR_OBJ): $(BAND_GPIO_LIVE_MONITOR_SRC)
	$(Q)mkdir -p $(dir $@)
	$(Q)echo "Compiling (debug) $< into $@"
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

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
	$(Q)echo "Compiling (release) $< into $@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $(@:.o=.d) -c $< -o $@

define MAKE_RELEASE_COMPONENT_RULES
$(OBJ_DIR_RELEASE)/$(patsubst ../../%,%,$(1))/%.o: $(1)/%.cpp
	$(Q)mkdir -p $$(@D)
	$(Q)echo "Compiling (release) $$< into $$@"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) -MF $$(@:.o=.d) -c $$< -o $$@
endef
$(foreach dir,$(COMPONENT_SRCDIRS),$(eval $(call MAKE_RELEASE_COMPONENT_RULES,$(dir))))

# Link release binary
$(PROFILE_RELEASE_BIN): $(CPP_OBJECTS) $(PROFILE_LINK_INPUTS)
	$(Q)mkdir -p $(PROFILE_BIN_DIR)
	$(Q)echo "Linking release: $(OUT) (libgpiod major: $(GPIOD_API_MAJOR))"
	$(Q)$(CXX) $(CXX_RELEASE_FLAGS) $(filter %.o,$^) -o $@ $(LDFLAGS) $(CONTROLLER_LINK_MAP)

ifneq ($(BUILD_PROFILE),controller)
$(BIN_DIR)/$(OUT): $(PROFILE_RELEASE_BIN) FORCE_PROFILE_BINARY
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)cp $(PROFILE_RELEASE_BIN) $@
endif

# ─────────────────────────────────────────────────────────────────────────────
# Phony targets
# ─────────────────────────────────────────────────────────────────────────────

.PHONY: clean debug test test-tone test-oneshot install-binary FORCE_PROFILE_BINARY
.PHONY: semantics-test rp1-web-backend-persistence-test mailbox-memory-flag-test legacy-gpio-clock-model-test gpio-frequency-correction-test machine-power-control-test websocket-lifecycle-test web-server-network-lifecycle-test wspr-band-catalog-test gpio-band-policy-test
.PHONY: support-request-guard-test privileged-network-policy-test backend-http-guard-test websocket-upgrade-guard-test apache-privileged-network-policy-test privileged-network-transaction-test privileged-network-runtime-test privileged-network-runtime-wiring-test privileged-network-admin-test support-bundle-job-manager-test support-bundle-result-validator-test
.PHONY: support-bundle-collector-executor-test support-bundle-runtime-test support-bundle-runtime-installer-test
.PHONY: installer-dependency-test rp1-gpclk-dkms-installer-test ui-publication-test support-bundle-age-dependency-test support-bundle-http-test support-bundle-web-server-wiring-test
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
.PHONY: support-bundle-collector-contract-test
.PHONY: support-bundle-intake-validation-test support-bundle-intake-state-test support-bundle-intake-retrieval-test
.PHONY: support-bundle-intake-controller-test support-bundle-intake-runtime-test support-bundle-intake-production-test
.PHONY: build-dependency-regression-test backend-capabilities-regression-test backend-capabilities-make-regression-test semantics-make-regression-test
.PHONY: backend-profile-factory-test update-check-comparison-test guarded-mode-change-persistence-test
.PHONY: band-gpio-default-convergence-test band-gpio-rotation-test selector-shutdown-cleanup-test
.PHONY: qualification-gpio-test-mode-test monitor-file-regression-test gpio-w
.PHONY: rp1-gpclk-planner-test rp1-gpclk-lifecycle-test rp1-gpclk-transition-test rp1-gpclk-development-policy-test rp1-gpclk-route-runtime-wiring-test rp1-gpclk-route-service-test
.PHONY: rp1-gpclk-diagnostics-test
DEBUG: debug
RELEASE: release

rp1-gpclk-diagnostics-test:
	$(Q)python3 ../scripts/tests/rp1_gpclk_diagnostics_test.py
	$(Q)python3 ../scripts/tests/rp1_provider_diagnostics_test.py
	$(Q)python3 -B ../scripts/tests/rp1_qualification_test.py

.PHONY: rp1-release-acceptance-test
rp1-release-acceptance-test:
	$(Q)python3 ../scripts/tests/rp1_release_acceptance_test.py

.PHONY: rp1-drive-cli-test
rp1-drive-cli-test: $(BIN_DIR)/$(SEMANTICS_TEST_OUT)
	$(Q)python3 -B ../scripts/tests/rp1_drive_cli_test.py $(BIN_DIR)/$(SEMANTICS_TEST_OUT)

clean:
	$(Q)echo "Cleaning up build artifacts."
	$(Q)$(SUDO) rm -rf build
	$(Q)$(SUDO) rm -rf ../executables

release: $(if $(filter controller,$(BUILD_PROFILE)),$(PROFILE_RELEASE_BIN),$(BIN_DIR)/$(OUT)) pairing-notices

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

.PHONY: controller-build-profile-test controller-profile-entry-test
controller-build-profile-test:
	$(Q)python3 ../scripts/tests/controller_build_profile_test.py
controller-profile-entry-test:
	$(Q)python3 ../scripts/tests/controller_profile_entry_test.py

.PHONY: controller-identity-store-test
controller-identity-store-test: $(BIN_DIR)/controller_identity_store_test
	$(Q)$(BIN_DIR)/controller_identity_store_test
$(BIN_DIR)/controller_identity_store_test: tests/controller_identity_store_test.cpp application_storage/controller_identity.cpp application_storage/controller_identity_record.hpp native_random.hpp owned_state_directory.cpp wtp_endpoint/identity.cpp wtp_integration/pi_management_journal.cpp WTP-Server/src/sha256.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. -IWTP-Server/include $(OPENSSL_CFLAGS) $(filter %.cpp,$^) $(OPENSSL_LIBS) -o $@

.PHONY: usb-cdc-windows-metadata-test
usb-cdc-windows-metadata-test: $(BIN_DIR)/usb_cdc_windows_metadata_test
	$(Q)$(BIN_DIR)/usb_cdc_windows_metadata_test
$(BIN_DIR)/usb_cdc_windows_metadata_test: tests/usb_cdc_windows_metadata_test.cpp wtp_integration/usb_cdc_windows_metadata.hpp wtp_integration/usb_cdc_native.hpp wtp_integration/usb_cdc.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -I. $(filter %.cpp,$^) -o $@

.PHONY: usb-cdc-descriptors-test
usb-cdc-descriptors-test: $(BIN_DIR)/usb_cdc_descriptors_test
	$(Q)$(BIN_DIR)/usb_cdc_descriptors_test
$(BIN_DIR)/usb_cdc_descriptors_test: tests/usb_cdc_descriptors_test.cpp wtp_integration/usb_cdc_descriptors.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. $^ -o $@

.PHONY: usb-cdc-native-test usb-cdc-native-inventory-test
usb-cdc-native-test: $(BIN_DIR)/usb_cdc_native_test
	$(Q)$(BIN_DIR)/usb_cdc_native_test
usb-cdc-native-inventory-test: $(BIN_DIR)/usb_cdc_native_test
	$(Q)$(BIN_DIR)/usb_cdc_native_test --native-inventory
$(BIN_DIR)/usb_cdc_native_test: tests/usb_cdc_native_test.cpp $(NATIVE_CDC_SOURCES) wtp_integration/usb_cdc.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. -IWTP-Client/include $^ $(NATIVE_USB_LIBS) -o $@

.PHONY: controller-transmitter-boundary-test
controller-transmitter-boundary-test: $(PROFILE_BIN_DIR)/controller_transmitter_boundary_test
	$(Q)$(PROFILE_BIN_DIR)/controller_transmitter_boundary_test
$(PROFILE_BIN_DIR)/controller_transmitter_boundary_test: tests/controller_transmitter_boundary_test.cpp transmitter_runtime_bridge_controller.cpp controller_local_hardware_unavailable.cpp local_frequency_provider.cpp $(BACKEND_CAPABILITIES_HEADER)
	$(Q)test "$(BUILD_PROFILE)" = controller
	$(Q)mkdir -p $(PROFILE_BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -UNDEBUG -Itests $(filter %.cpp,$^) -o $@

semantics-make-regression-test:
	$(Q)python3 $(SEMANTICS_MAKE_REGRESSION_TEST)

.PHONY: ci-change-scope-test
ci-change-scope-test:
	$(Q)python3 -B ../scripts/tests/ci_change_scope_test.py

.PHONY: wtp-client-host-build wtp-protocol-test
wtp-client-host-build:
	$(Q)$(MAKE) --no-print-directory -C WTP-Client all

wtp-protocol-test: wtp-client-host-build
	$(Q)$(MAKE) --no-print-directory -C WTP-Client test

# One recursive archive build per parent Make invocation, before any consumers.
.PHONY: wtp-server-host-build
wtp-server-host-build:
	$(Q)$(MAKE) --no-print-directory -C WTP-Server all

# Portable native ownership model; inhibited engine and injected clock only.
.PHONY: wtp-server-core-test
wtp-server-core-test: wtp-server-host-build
	$(Q)$(MAKE) --no-print-directory -C WTP-Server test

.PHONY: wtp-pi-authority-test
wtp-pi-authority-test: wtp-server-host-build
	$(Q)$(MAKE) --no-print-directory -C WTP-Server test
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. -IWTP-Server/include \
		tests/wtp_pi_authority_test.cpp wtp_endpoint/authority.cpp \
		wtp_endpoint/revocation_journal.cpp \
		WTP-Server/build/libwtp_server.a -o build/wtp_pi_authority_test
	$(Q)build/wtp_pi_authority_test

.PHONY: wtp-pi-revocation-test
wtp-pi-revocation-test:
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. \
		tests/wtp_pi_revocation_journal_test.cpp \
		wtp_endpoint/revocation_journal.cpp -o build/wtp_pi_revocation_journal_test
	$(Q)build/wtp_pi_revocation_journal_test

.PHONY: wtp-pi-tone-engine-test
wtp-pi-tone-engine-test:
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. -IWTP-Server/include \
		tests/wtp_pi_tone_engine_test.cpp wtp_endpoint/tone_engine.cpp \
		-o build/wtp_pi_tone_engine_test
	$(Q)build/wtp_pi_tone_engine_test

.PHONY: wtp-pi-identity-test
wtp-pi-identity-test:
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. -IWTP-Server/include \
		tests/wtp_pi_identity_test.cpp wtp_endpoint/identity.cpp \
		WTP-Server/src/sha256.cpp -o build/wtp_pi_identity_test $(if $(filter Windows_NT,$(OS)),-lbcrypt,)
	$(Q)build/wtp_pi_identity_test

.PHONY: wtp-pi-interface-selection-test
wtp-pi-interface-selection-test:
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. \
		tests/wtp_pi_interface_selection_test.cpp \
		wtp_endpoint/interface_selection.cpp -o build/wtp_pi_interface_selection_test
	$(Q)build/wtp_pi_interface_selection_test

.PHONY: wtp-pi-si5351-realization-test
wtp-pi-si5351-realization-test:
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. \
		tests/wtp_pi_si5351_realization_test.cpp \
		wtp_endpoint/si5351_realization.cpp \
		WSPR-Transmitter/src/si5351_planner.cpp \
		-o build/wtp_pi_si5351_realization_test
	$(Q)build/wtp_pi_si5351_realization_test

.PHONY: wtp-pi-control-test
wtp-pi-control-test:
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. \
		tests/wtp_pi_control_test.cpp wtp_pi_control.cpp \
		-o build/wtp_pi_control_test
	$(Q)build/wtp_pi_control_test

WTP_USB_BUILD_DIR ?= build/wtp-usb
WTP_USB_CXXFLAGS ?= -O2 -g
WTP_USB_LDFLAGS ?=
.PHONY: wtp-usb-test
wtp-usb-test:
	$(Q)$(MAKE) --no-print-directory -C WTP-Client all BUILD_DIR="$(abspath $(WTP_USB_BUILD_DIR)/client)" CXXFLAGS="$(WTP_USB_CXXFLAGS)"
	$(Q)mkdir -p "$(WTP_USB_BUILD_DIR)"
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror $(WTP_USB_CXXFLAGS) $(OPENSSL_CFLAGS) -I. -IWTP-Client/include \
		tests/wtp_usb_cdc_test.cpp wtp_integration/usb_cdc.cpp wtp_integration/usb_discovery.cpp \
		$(NATIVE_CDC_SOURCES) $(OPENSSL_LIBS) $(NATIVE_USB_LIBS) $(WTP_USB_LDFLAGS) -o "$(WTP_USB_BUILD_DIR)/wtp_usb_cdc_test"
	$(Q)"$(WTP_USB_BUILD_DIR)/wtp_usb_cdc_test"

backend-profile-factory-test: $(BACKEND_PROFILE_FACTORY_TEST_BIN)
	$(Q)./$(BACKEND_PROFILE_FACTORY_TEST_BIN)

$(MACHINE_POWER_UNAVAILABLE_TEST_BIN): $(MACHINE_POWER_UNAVAILABLE_TEST_SRC) machine_power_control_unavailable.cpp machine_power_control.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++$(CXXVER) -Wall -Wextra -Werror $(MACHINE_POWER_UNAVAILABLE_TEST_SRC) machine_power_control_unavailable.cpp -o $@

machine-power-control-test: $(MACHINE_POWER_UNAVAILABLE_TEST_BIN)
	$(Q)./$(MACHINE_POWER_UNAVAILABLE_TEST_BIN)
	$(Q)python3 $(MACHINE_POWER_REGRESSION_TEST)

debug: $(if $(filter controller,$(BUILD_PROFILE)),$(PROFILE_DEBUG_BIN),$(BIN_DIR)/$(TEST_OUT)) pairing-notices
	$(Q)echo "Debug build completed successfully."

test: debug
	$(Q)echo "Running test with sudo privileges."
	$(Q)$(SUDO) ./$(BIN_DIR)/$(TEST_OUT) -D --state-directory "$(STATE_DIRECTORY)"

test-tone: debug
	$(Q)echo "Running test tone with sudo privileges."
	$(Q)$(SUDO) ./$(BIN_DIR)/$(TEST_OUT) -D --test-tone $(TEST_FREQ)

test-oneshot: debug
	$(Q)echo "Running test oneshot with sudo privileges."
	$(Q)$(SUDO) ./$(BIN_DIR)/$(TEST_OUT) -D -x 1 $(WSPR_PARAMS)

.PHONY: semantics-test semantics-test-full semantics-test-portable semantics-test-portable-profile

.PHONY: legacy-tone-frequency-test
legacy-tone-frequency-test:
	$(Q)$(MAKE) -C WSPR-Transmitter/src legacy-tone-frequency-test SUDO=

semantics-test-full semantics-test-portable-profile: wtp-host-reference-test native-controller-owners-test recovery-context-busy-test legacy-tone-frequency-test sqlite-state-store-test persistent-configuration-test sqlite-catalog-test state-migration-test companion-files-test application-companion-capture-test sqlite-state-entry-test recovery-capture-backend-test recovery-age-unit-test recovery-archive-test recovery-publication-test restored-generation-test recovery-maintenance-test recovery-evidence-test recovery-destination-settings-test recovery-browser-admission-test recovery-jobs-test recovery-worker-process-test diagnostic-export-test browser-http-resource-test web-bootstrap-test local-control-transport-test local-event-feed-test database-configuration-runtime-test database-managed-startup-test database-fresh-startup-test controller-settings-test controller-retirement-storage-binding-test database-controller-runtime-test
ifeq ($(HOST_OS),Linux)
semantics-test-portable-profile: si5351-inventory-process-test
endif

# Keep one stable meaning for the broad suite: Linux compilation and policy
# coverage for the default physical and simulated backends. The recipe performs
# its checks before a recursive make can begin compiling prerequisites.
semantics-test:
	$(Q)if [ "$(HOST_OS)" != "Linux" ]; then \
		echo "Full semantics requires Linux and the libgpiod C++ development headers." >&2; \
		echo "Run 'make semantics-test-portable' for the hardware-free simulated profile on $(HOST_OS)." >&2; \
		exit 2; \
	fi
	$(Q)if [ "$(GPIOD_CPP_HEADER_AVAILABLE)" != "1" ]; then \
		echo "Full semantics requires gpiod.hpp (install the libgpiod C++ development package)." >&2; \
		echo "Run 'make semantics-test-portable' for the hardware-free simulated profile." >&2; \
		exit 2; \
	fi
	$(Q)$(MAKE) --no-print-directory semantics-test-full \
		BACKENDS=$(DEFAULT_BACKENDS) ANCILLARY_GPIO=1

semantics-test-full: solar-clock-test recurrence-progress-test $(BIN_DIR)/native_recurrence_test $(BIN_DIR)/native_recurrence_main_fixture scheduled-launch-test recurrence-authority-record-test solar-geometry-test solar-policy-test linux-i2c-identity-test linux-i2c-identity-config-test wspr-plan-placement-test test-tone-lifecycle-test si5351-inventory-process-test si5351-inventory-isolation-test calibration-roundtrip-test i2c-bus-inventory-test i2c-bus-selection-test chipset-offsets-test mailbox-memory-flag-test legacy-gpio-clock-model-test gpio-frequency-correction-test rp1-web-backend-persistence-test websocket-lifecycle-test web-server-network-lifecycle-test $(BIN_DIR)/$(SEMANTICS_TEST_OUT) $(BIN_DIR)/$(CLEANUP_LIFECYCLE_TEST_OUT) $(BIN_DIR)/$(UI_SOURCE_REGRESSION_TEST_OUT) $(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT)
	# Run deadline-sensitive launch checks after all parallel builds/tests finish.
	$(Q)$(MAKE) --no-print-directory -j1 native-recurrence-test native-recurrence-main-test
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
	$(Q)node ../WsprryPi-UI/tests/gpio_correction_provenance_test.js
	$(Q)echo "Running coherent UI publication tests."
	$(Q)python3 ../scripts/tests/ui_publication_test.py

# This is an explicit portable subset, not a transparent fallback from the full
# suite. It compiles the real application with only the simulated backend and
# retains capability-neutral source, policy, persistence, and publication
# checks. Backend-specific runtime semantics and cleanup lifecycle coverage stay
# in semantics-test-full because their current assertions require physical
# backend capabilities.
semantics-test-portable:
	$(Q)echo "Running portable semantics with BACKENDS=simulated ANCILLARY_GPIO=0."
	$(Q)$(MAKE) --no-print-directory semantics-test-portable-profile \
		BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=

semantics-test-portable-profile: solar-clock-test recurrence-progress-test $(BIN_DIR)/native_recurrence_test $(BIN_DIR)/native_recurrence_main_fixture scheduled-launch-test recurrence-authority-record-test solar-geometry-test solar-policy-test linux-i2c-identity-test linux-i2c-identity-config-test wspr-plan-placement-test test-tone-lifecycle-test si5351-inventory-isolation-test calibration-roundtrip-test idle-startup-policy-test i2c-bus-inventory-test i2c-bus-selection-test backend-capabilities-regression-test backend-capabilities-make-regression-test debug band-lookup-correlation-test chipset-offsets-test mailbox-memory-flag-test legacy-gpio-clock-model-test gpio-frequency-correction-test websocket-lifecycle-test web-server-network-lifecycle-test $(BIN_DIR)/$(UI_SOURCE_REGRESSION_TEST_OUT) $(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT)
	# Run deadline-sensitive launch checks after all parallel builds/tests finish.
	$(Q)$(MAKE) --no-print-directory -j1 native-recurrence-test native-recurrence-main-test
	$(Q)echo "Running portable UI/source regression tests."
	$(Q)./$(BIN_DIR)/$(UI_SOURCE_REGRESSION_TEST_OUT)
	$(Q)echo "Running portable GPIO band fail-closed policy tests."
	$(Q)./$(BIN_DIR)/$(GPIO_BAND_POLICY_TEST_OUT)
	$(Q)echo "Running portable log timestamp display regression tests."
	$(Q)node tests/log_timestamp_display_test.js
	$(Q)echo "Running portable update-check comparison tests."
	$(Q)node tests/update_check_comparison_test.js
	$(Q)node ../WsprryPi-UI/tests/gpio_correction_provenance_test.js
	$(Q)echo "Running portable coherent UI publication tests."
	$(Q)python3 ../scripts/tests/ui_publication_test.py

rp1-web-backend-persistence-test: $(BIN_DIR)/$(SEMANTICS_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SEMANTICS_TEST_OUT) --rp1-web-backend-persistence-only

$(BIN_DIR)/$(LEGACY_GPIO_CLOCK_MODEL_TEST_OUT): WSPR-Transmitter/src/legacy_gpio_clock_model_test.cpp WSPR-Transmitter/src/legacy_gpio_clock_model.cpp Chipset-Offsets/src/chipset_offsets.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(LEGACY_GPIO_CLOCK_MODEL_TEST_FLAGS) $(filter %.cpp,$^) -o $@

.PHONY: chipset-offsets-test
chipset-offsets-test:
	$(Q)$(MAKE) -C Chipset-Offsets test

legacy-gpio-clock-model-test: $(BIN_DIR)/$(LEGACY_GPIO_CLOCK_MODEL_TEST_OUT)
	$(Q)echo "Running hardware-independent legacy GPIO clock-model tests."
	$(Q)./$(BIN_DIR)/$(LEGACY_GPIO_CLOCK_MODEL_TEST_OUT)

$(BIN_DIR)/$(MAILBOX_MEMORY_FLAG_TEST_OUT): tests/mailbox_memory_flag_test.cpp Mailbox/src/mailbox_revision.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(MAILBOX_MEMORY_FLAG_TEST_FLAGS) $^ -o $@

mailbox-memory-flag-test: $(BIN_DIR)/$(MAILBOX_MEMORY_FLAG_TEST_OUT)
	$(Q)echo "Running mailbox memory flag tests."
	$(Q)./$(BIN_DIR)/$(MAILBOX_MEMORY_FLAG_TEST_OUT)

$(BIN_DIR)/$(GPIO_FREQUENCY_CORRECTION_TEST_OUT): tests/gpio_frequency_correction_test.cpp system_clock_frequency_estimate.cpp WSPR-Transmitter/src/legacy_gpio_clock_model.cpp system_clock_frequency_estimate.hpp PPM-Manager/src/ppm_manager.hpp Chipset-Offsets/src/chipset_offsets.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(GPIO_FREQUENCY_CORRECTION_TEST_FLAGS) $(filter %.cpp,$^) -o $@

gpio-frequency-correction-test: $(BIN_DIR)/$(GPIO_FREQUENCY_CORRECTION_TEST_OUT) chrony-frequency-correction-test rp1-pps-calibration-test
	$(Q)echo "Running hardware-independent GPIO frequency-correction tests."
	$(Q)./$(BIN_DIR)/$(GPIO_FREQUENCY_CORRECTION_TEST_OUT)

.PHONY: rp1-pps-calibration-test
rp1-pps-calibration-test: $(BIN_DIR)/rp1_pps_calibration_test
	$(Q)python3 ../scripts/tests/rp1_pps_vendor_test.py
	$(Q)./$(BIN_DIR)/rp1_pps_calibration_test

$(BIN_DIR)/rp1_pps_calibration_test: tests/rp1_pps_calibration_test.cpp rp1_pps_calibration.cpp system_clock_frequency_estimate.cpp WSPR-Transmitter/src/legacy_gpio_clock_model.cpp rp1_pps_calibration.hpp vendor/rp1_pio/rp1_pio_if.h
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(GPIO_FREQUENCY_CORRECTION_TEST_FLAGS) $(filter %.cpp,$^) -lpthread -o $@

$(BIN_DIR)/chrony_frequency_correction_test: tests/chrony_frequency_correction_test.cpp system_clock_frequency_estimate.cpp PPM-Manager/src/ppm_manager.cpp WSPR-Transmitter/src/legacy_gpio_clock_model.cpp system_clock_frequency_estimate.hpp PPM-Manager/src/ppm_manager.hpp Chipset-Offsets/src/chipset_offsets.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(GPIO_FREQUENCY_CORRECTION_TEST_FLAGS) $(filter %.cpp,$^) -lpthread $(ATOMIC_LIB) -o $@

.PHONY: chrony-frequency-correction-test
chrony-frequency-correction-test: $(BIN_DIR)/chrony_frequency_correction_test
	$(Q)./$(BIN_DIR)/chrony_frequency_correction_test

websocket-lifecycle-test: $(BIN_DIR)/$(WEBSOCKET_LIFECYCLE_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(WEBSOCKET_LIFECYCLE_TEST_OUT)

web-server-network-lifecycle-test: $(BIN_DIR)/$(WEB_SERVER_NETWORK_LIFECYCLE_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(WEB_SERVER_NETWORK_LIFECYCLE_TEST_OUT)

# Identity is injected only in the fixture launcher; every service and storage
# operation is production code, with no output backend selected.
$(OBJ_DIR_DEBUG)/tests/controller_only_fixture.o: tests/controller_only_fixture.cpp
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -MF $(@:.o=.d) -c $< -o $@

$(BIN_DIR)/controller_only_fixture: $(OBJ_DIR_DEBUG)/tests/controller_only_fixture.o $(filter-out $(MAIN_DEBUG_OBJECT),$(CPP_DEBUG_OBJECTS))
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

.PHONY: controller-only-test
controller-only-test: $(BIN_DIR)/controller_only_fixture $(CONTROLLER_PRODUCTION_FIXTURE)
	$(Q)python3 -B tests/controller_only_test.py --fixture $(BIN_DIR)/controller_only_fixture
	$(Q)python3 -B tests/controller_only_test.py --fixture $(CONTROLLER_PRODUCTION_FIXTURE) --production-main --private-state

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

.PHONY: php2shell-test
php2shell-test:
	$(Q)python3 -B ../scripts/tests/php2shell_mitigate_test.py

rp1-gpclk-dkms-installer-test:
	$(Q)python3 -B ../scripts/tests/rp1_gpclk_dkms_install_test.py
	$(Q)python3 -B ../scripts/tests/rp1_installer_shell_test.py
	$(Q)python3 -B ../scripts/tests/install_ownership_test.py

.PHONY: rp1-gpclk-update-contract-test
rp1-gpclk-update-contract-test:
	@test -n "$(RP1_PROVIDER_SOURCE)" || { echo "RP1_PROVIDER_SOURCE=/path/to/RP1-GPIO-CLK-DKMS is required"; exit 2; }
	$(Q)python3 ../scripts/tests/rp1_gpclk_update_contract_test.py "$(RP1_PROVIDER_SOURCE)"

ui-publication-test:
	$(Q)python3 ../scripts/tests/ui_publication_test.py

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

support-bundle-collector-contract-test:
	$(Q)bash $(SUPPORT_BUNDLE_COLLECTOR_CONTRACT_TEST)

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

$(BIN_DIR)/$(RP1_GPCLK_PLANNER_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_planner_test.cpp WSPR-Transmitter/src/rp1_gpclk_planner.cpp WSPR-Transmitter/src/rp1_gpclk_planner.hpp Chipset-Offsets/src/chipset_offsets.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $(filter %.cpp,$^) -o $@

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

$(BIN_DIR)/$(RP1_GPCLK_BACKEND_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_backend_test.cpp WSPR-Transmitter/src/rp1_gpclk_backend.cpp WSPR-Transmitter/src/rp1_gpclk_planner.cpp WSPR-Transmitter/src/rp1_gpclk_reconciliation.cpp WSPR-Transmitter/src/rp1_gpclk_backend.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $(filter %.cpp,$^) -o $@

rp1-gpclk-backend-test: $(BIN_DIR)/$(RP1_GPCLK_BACKEND_TEST_OUT)
	$(Q)echo "Running clock-disabled RP1 GPCLK production backend contract tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_BACKEND_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_LINUX_PROVIDER_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_linux_provider_test.cpp WSPR-Transmitter/src/rp1_gpclk_linux_provider.cpp WSPR-Transmitter/src/rp1_gpclk_reconciliation.cpp WSPR-Transmitter/src/rp1_gpclk_development_policy.cpp WSPR-Transmitter/src/rp1_gpclk_consumer_contract.hpp WSPR-Transmitter/src/rp1_gpclk_linux_provider.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $(filter %.cpp,$^) -o $@

rp1-gpclk-linux-provider-test: $(BIN_DIR)/$(RP1_GPCLK_LINUX_PROVIDER_TEST_OUT)
	$(Q)echo "Running address-independent RP1 GPCLK Linux provider client tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_LINUX_PROVIDER_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_DEVELOPMENT_POLICY_TEST_OUT): WSPR-Transmitter/src/rp1_gpclk_development_policy_test.cpp WSPR-Transmitter/src/rp1_gpclk_development_policy.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $^ -o $@

rp1-gpclk-development-policy-test: $(BIN_DIR)/$(RP1_GPCLK_DEVELOPMENT_POLICY_TEST_OUT)
	$(Q)echo "Running hardware-independent guarded RP1 GPCLK development policy tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_DEVELOPMENT_POLICY_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_ROUTE_RUNTIME_WIRING_TEST_OUT): tests/rp1_gpclk_route_runtime_wiring_test.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $< -o $@

rp1-gpclk-route-runtime-wiring-test: $(BIN_DIR)/$(RP1_GPCLK_ROUTE_RUNTIME_WIRING_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_ROUTE_RUNTIME_WIRING_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_ROUTE_SERVICE_TEST_OUT): tests/rp1_gpclk_route_service_test.cpp rp1_gpclk_route_service.cpp WSPR-Transmitter/src/rp1_gpclk_development_policy.cpp rp1_provider_process.hpp rp1_gpclk_route_service.hpp rp1_runtime_ownership.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) -DWSPRRYPI_ROUTE_SERVICE_TEST $(filter %.cpp,$^) -o $@

$(BIN_DIR)/rp1_provider_process_test: tests/rp1_provider_process_test.cpp rp1_provider_process.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $< -o $@

$(BIN_DIR)/rp1_runtime_ownership_test: tests/rp1_runtime_ownership_test.cpp rp1_runtime_ownership.hpp WSPR-Transmitter/src/rp1_gpclk_consumer_contract.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $< -o $@

rp1-gpclk-route-service-test: $(BIN_DIR)/$(RP1_GPCLK_ROUTE_SERVICE_TEST_OUT) $(BIN_DIR)/rp1_provider_process_test $(BIN_DIR)/rp1_runtime_ownership_test
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_ROUTE_SERVICE_TEST_OUT)
	$(Q)./$(BIN_DIR)/rp1_provider_process_test
	$(Q)./$(BIN_DIR)/rp1_runtime_ownership_test
	$(Q)python3 ../scripts/tests/route_application_test.py
	$(Q)python3 ../scripts/tests/runtime_reconcile_test.py

$(BIN_DIR)/$(RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT): WSPR-Transmitter/src/execution_plan_compiler.cpp WSPR-Transmitter/src/rp1_gpclk_transmit_backend_test.cpp WSPR-Transmitter/src/rp1_gpclk_transmit_backend.cpp WSPR-Transmitter/src/rp1_gpclk_development_policy.cpp WSPR-Transmitter/src/rp1_gpclk_event_program.cpp WSPR-Transmitter/src/rp1_gpclk_backend.cpp WSPR-Transmitter/src/rp1_gpclk_linux_provider.cpp WSPR-Transmitter/src/rp1_gpclk_planner.cpp WSPR-Transmitter/src/rp1_gpclk_planner.hpp WSPR-Transmitter/src/rp1_gpclk_reconciliation.cpp Chipset-Offsets/src/chipset_offsets.hpp WSPR-Transmitter/src/rp1_gpclk_consumer_contract.hpp WSPR-Transmitter/src/rp1_gpclk_backend.hpp WSPR-Transmitter/src/rp1_gpclk_transmit_backend.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $(OPENSSL_CFLAGS) -pthread $(filter %.cpp,$^) $(OPENSSL_LIBS) -o $@

rp1-gpclk-transmit-backend-test: $(BIN_DIR)/$(RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT)
	$(Q)echo "Running RP1 GPCLK scheduler backend tests."
	$(Q)./$(BIN_DIR)/$(RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT)

$(BIN_DIR)/$(RP1_GPCLK_ADMIN_PROBE_OUT): WSPR-Transmitter/src/rp1_gpclk_admin_probe.cpp WSPR-Transmitter/src/rp1_gpclk_linux_provider.cpp WSPR-Transmitter/src/rp1_gpclk_development_policy.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(RP1_GPCLK_PLANNER_TEST_FLAGS) $^ -o $@

rp1-gpclk-admin-probe: $(BIN_DIR)/$(RP1_GPCLK_ADMIN_PROBE_OUT)
	$(Q)echo "Built output-disabled RP1 GPCLK administrative probe; execution requires separate authorization."

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
	$(Q)$(SUDO) gdb --args ./$(BIN_DIR)/$(TEST_OUT) -D --state-directory "$(STATE_DIRECTORY)"

install-binary: release install-pairing-notices
	$(Q)echo "Installing $(OUT) binary to $(PREFIX)."
	$(Q)$(SUDO) install -d "$(PREFIX)"
	$(Q)$(SUDO) install -m 755 "$(BIN_DIR)/$(OUT)" "$(PREFIX)/$(OUT)"
	$(Q)echo "Binary installation complete: $(PREFIX)/$(OUT)"

install: release install-pairing-notices
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

debuginstall: debug install-pairing-notices
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
	$(Q)echo "  release                 Build optimized binary"
	$(Q)echo "  debug                   Build debug binary"
	$(Q)echo "  test                    Run tests"
	$(Q)echo "  semantics-test          Run the full Linux semantics suite (requires gpiod.hpp)"
	$(Q)echo "  semantics-test-portable Run the simulated-only portable semantics subset"
	$(Q)echo "  wtp-protocol-test       Run standalone WTP/1 wire and session tests"
	$(Q)echo "  wtp-production-test     Run injected parent configuration/runtime and UI tests"
	$(Q)echo "  wtp-usb-test            Run WTP USB adapter tests using fakes and PTYs only"
	$(Q)echo "  gdb                     Debug with gdb"
	$(Q)echo "  test-tone               Run the binary with a test tone"
	$(Q)echo "  test-oneshot            Run the binary with one iteration"
	$(Q)echo "  install-binary          Install only the selected release binary to $(PREFIX)"
	$(Q)echo "  install                 Install the binary to $(PREFIX)"
	$(Q)echo "  debuginstall            Install the debug binary to $(PREFIX)"
	$(Q)echo "  uninstall               Remove the installed binary"
	$(Q)echo "  lint                    Run static analysis"
	$(Q)echo "  macros                  Show macros"
	$(Q)echo "  clean                   Remove build artifacts"
	$(Q)echo "  help                    This message"

.PHONY: i2c-bus-inventory-test i2c-bus-selection-test
i2c-bus-inventory-test: $(BIN_DIR)/i2c_bus_inventory_test
	$(Q)./$(BIN_DIR)/i2c_bus_inventory_test

$(BIN_DIR)/i2c_bus_inventory_test: tests/i2c_bus_inventory_test.cpp i2c_bus_inventory.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++17 -Wall -Wextra -pedantic tests/i2c_bus_inventory_test.cpp -o $@

i2c-bus-selection-test: $(BIN_DIR)/$(SEMANTICS_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SEMANTICS_TEST_OUT) --i2c-bus-selection-only

.PHONY: idle-startup-policy-test calibration-roundtrip-test
calibration-roundtrip-test: $(BIN_DIR)/$(SEMANTICS_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SEMANTICS_TEST_OUT) --calibration-roundtrip-only

idle-startup-policy-test: $(BIN_DIR)/$(SEMANTICS_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SEMANTICS_TEST_OUT) --idle-startup-overrides-only

.PHONY: wtp-production-test
WTP_CATALOG_BUILD_DIR ?= build/wtp-catalog
.PHONY: wtp-catalog-test
wtp-catalog-test:
	$(Q)mkdir -p "$(WTP_CATALOG_BUILD_DIR)"
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -O2 $(OPENSSL_CFLAGS) -I. \
		tests/wtp_catalog_discovery_test.cpp wtp_integration/catalog.cpp wtp_integration/discovery.cpp \
		$(OPENSSL_LIBS) -o "$(WTP_CATALOG_BUILD_DIR)/wtp_catalog_discovery_test"
	$(Q)"$(WTP_CATALOG_BUILD_DIR)/wtp_catalog_discovery_test"
$(BIN_DIR)/wtp_production_test: tests/wtp_retirement_runtime_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
wtp-production-cpp-test: $(BIN_DIR)/wtp_production_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./$(BIN_DIR)/wtp_production_test

wtp-production-test: wtp-production-cpp-test wtp-ui-test



.PHONY: wtp-managed-output-test
$(BIN_DIR)/wtp_managed_output_test: tests/wtp_managed_output_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
wtp-managed-output-test: $(BIN_DIR)/wtp_managed_output_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/wtp_managed_output_test"

.PHONY: pico-management-target-test
$(BIN_DIR)/pico_management_target_test: tests/pico_management_target_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
$(BIN_DIR)/pi_pair_browser_fixture: tests/pi_pair_browser_fixture.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
$(BIN_DIR)/pico_management_pair_client: tests/pico_management_pair_client.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
$(BIN_DIR)/pico_management_usb_pair_client: tests/pico_management_usb_pair_client.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
$(BIN_DIR)/pico_controller_pair_fixture: tests/pico_controller_pair_fixture.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pico-management-target-test: $(BIN_DIR)/pico_management_target_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pico_management_target_test"

.PHONY: pico-management-journal-test
.PHONY: pico-management-history-test
$(BIN_DIR)/pico_management_history_test: tests/pico_management_history_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pico-management-history-test: $(BIN_DIR)/pico_management_history_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pico_management_history_test"

.PHONY: pico-usb-settings-test
$(BIN_DIR)/pico_usb_settings_test: tests/pico_usb_settings_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pico-usb-settings-test: $(BIN_DIR)/pico_usb_settings_test
	$(Q)"$(BIN_DIR)/pico_usb_settings_test"

$(BIN_DIR)/pico_management_journal_test: tests/pico_management_journal_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pico-management-journal-test: $(BIN_DIR)/pico_management_journal_test
	$(Q)"$(BIN_DIR)/pico_management_journal_test"

.PHONY: pico-target-controller-test
$(BIN_DIR)/pico_target_controller_test: tests/pico_target_controller_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pico-target-controller-test: $(BIN_DIR)/pico_target_controller_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pico_target_controller_test"

.PHONY: controller-contract-test
.PHONY: pi-config-persistence-coordinator-test
.PHONY: pi-management-anchor-test
.PHONY: pi-management-journal-test
.PHONY: pi-managed-store-test
.PHONY: pi-management-service-test
.PHONY: pi-managed-startup-test
.PHONY: pi-managed-host-test
.PHONY: pi-managed-host-runtime-test
.PHONY: pi-private-network-persistence-test
.PHONY: pi-managed-main-test
.PHONY: sqlite-state-entry-test
sqlite-state-entry-test: $(CONTROLLER_PRODUCTION_FIXTURE)
	$(Q)python3 -B tests/sqlite_state_entry_test.py $(CONTROLLER_PRODUCTION_FIXTURE)

.PHONY: web-bootstrap-test
$(BIN_DIR)/web_bootstrap_test: tests/web_bootstrap_test.cpp application_control/web_bootstrap.cpp application_control/web_bootstrap.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. $(filter %.cpp,$^) -o $@
web-bootstrap-test: $(BIN_DIR)/web_bootstrap_test
	$(Q)"$(BIN_DIR)/web_bootstrap_test"

.PHONY: browser-http-resource-test
$(BIN_DIR)/browser_http_resource_test: tests/browser_http_resource_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
browser-http-resource-test: $(BIN_DIR)/browser_http_resource_test
	$(Q)"$(BIN_DIR)/browser_http_resource_test"

.PHONY: browser-session-backend-test
$(BIN_DIR)/browser_session_backend_fixture: tests/browser_session_backend_fixture.cpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
browser-session-backend-test: $(BIN_DIR)/browser_session_backend_fixture
	$(Q)python3 -B tests/browser_session_backend_test.py $(BIN_DIR)/browser_session_backend_fixture

.PHONY: local-event-feed-test
$(BIN_DIR)/local_event_feed_test: tests/local_event_feed_test.cpp application_control/event_feed.cpp application_control/event_feed.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -pthread -I. $(filter %.cpp,$^) -o $@
local-event-feed-test: $(BIN_DIR)/local_event_feed_test
	$(Q)"$(BIN_DIR)/local_event_feed_test"

.PHONY: local-control-transport-test
$(BIN_DIR)/local_control_transport_test: tests/local_control_transport_test.cpp tests/software_child_process.hpp application_control/local_transport.cpp application_control/local_transport.hpp application_control/local_protocol.hpp application_control/local_session.hpp native_random.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -pthread -I. -Itests $(filter %.cpp,$^) -o $@
local-control-transport-test: $(BIN_DIR)/local_control_transport_test
	$(Q)"$(BIN_DIR)/local_control_transport_test"
.PHONY: pi-managed-wtp-main-test
$(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o: main.cpp controller_instance_lock.hpp Singleton/src/installation_lock.hpp wtp_endpoint/runtime.hpp $(BUILD_METADATA_HEADER) $(BACKEND_CAPABILITIES_HEADER)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Dmain=pi_managed_production_main -MF $(@:.o=.d) -c $< -o $@
$(CONTROLLER_PRODUCTION_FIXTURE): tests/controller_only_production_fixture.cpp tests/software_control_runtime.hpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS) $(PROFILE_LINK_INPUTS)
	$(Q)mkdir -p $(dir $@)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
$(BIN_DIR)/pi_managed_main_fixture: tests/pi_managed_main_fixture.cpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
$(BIN_DIR)/native_recurrence_main_fixture: tests/pi_managed_main_fixture.cpp tests/simulated_launch_latch.hpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests -DWSPRRYPI_NATIVE_RECURRENCE_FIXTURE $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
# Two isolated actual-main targets with independent identity and approved typed
# clock-health fixtures; production identity itself is exercised elsewhere.
$(BIN_DIR)/scheduling_autonomy_fixture: tests/scheduling_autonomy_fixture.cpp main.cpp $(filter-out %/wtp_endpoint/identity.o,$(SEMANTICS_TEST_DEPS))
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$(filter-out main.cpp,$^)) -o $@ $(LDFLAGS)

$(addprefix $(BIN_DIR)/,pi_managed_main_fixture native_recurrence_main_fixture pi_managed_wtp_main_fixture shared_setup_main_fixture scheduling_autonomy_fixture): tests/software_control_runtime.hpp

.PHONY: scheduling-autonomy-test
scheduling-autonomy-test: $(BIN_DIR)/scheduling_autonomy_fixture
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/scheduling_autonomy_test.py --fixture "$(abspath $(BIN_DIR)/scheduling_autonomy_fixture)" --output "$(abspath ../build/scheduling-autonomy)"

pi-managed-main-test: $(BIN_DIR)/pi_managed_main_fixture
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/pi_managed_main_test.py --fixture "$(BIN_DIR)/pi_managed_main_fixture"
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/pi_managed_main_test.py --fixture "$(BIN_DIR)/pi_managed_main_fixture" --network-safety-off
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/pi_managed_main_test.py --fixture "$(BIN_DIR)/pi_managed_main_fixture" --initialization
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/pi_managed_main_test.py --fixture "$(BIN_DIR)/pi_managed_main_fixture" --policy-upgrade

$(BIN_DIR)/shared_setup_main_fixture: tests/shared_setup_main_fixture.cpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)

$(BIN_DIR)/pi_managed_wtp_main_fixture: tests/pi_managed_wtp_main_fixture.cpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
# Actual-main fixtures deliberately retain the production singleton boundary.
# Serialize them even when the parent validation target uses parallel make.
pi-managed-wtp-main-test: pi-managed-main-test $(BIN_DIR)/pi_managed_wtp_main_fixture
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/pi_managed_wtp_main_test.py --fixture "$(BIN_DIR)/pi_managed_wtp_main_fixture"

$(BIN_DIR)/pi_private_network_persistence_test: tests/pi_private_network_persistence_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-private-network-persistence-test: $(BIN_DIR)/pi_private_network_persistence_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_private_network_persistence_test"

$(BIN_DIR)/pi_managed_host_runtime_test: tests/pi_managed_host_runtime_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-managed-host-runtime-test: $(BIN_DIR)/pi_managed_host_runtime_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_managed_host_runtime_test"

$(BIN_DIR)/pi_managed_host_test: tests/pi_managed_host_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-managed-host-test: $(BIN_DIR)/pi_managed_host_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_managed_host_test"

$(BIN_DIR)/pi_managed_startup_test: tests/pi_managed_startup_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-managed-startup-test: $(BIN_DIR)/pi_managed_startup_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_managed_startup_test" wtp_integration/contract/control-controller-vectors.json

$(BIN_DIR)/pi_management_service_test: tests/pi_management_service_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-management-service-test: $(BIN_DIR)/pi_management_service_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_management_service_test"

$(BIN_DIR)/pi_pair_contract_test: tests/pi_pair_contract_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-contract-test
pi-pair-contract-test: $(BIN_DIR)/pi_pair_contract_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_contract_test"

$(BIN_DIR)/pi_pair_crypto_test: tests/pi_pair_crypto_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-crypto-test
pi-pair-crypto-test: $(BIN_DIR)/pi_pair_crypto_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_crypto_test"

$(BIN_DIR)/pi_pair_records_test: tests/pi_pair_records_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-records-test
pi-pair-records-test: $(BIN_DIR)/pi_pair_records_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_records_test"

$(BIN_DIR)/pi_pair_tls_test: tests/pi_pair_tls_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-tls-test
pi-pair-tls-test: $(BIN_DIR)/pi_pair_tls_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_tls_test"

$(BIN_DIR)/local_browser_principal_test: tests/local_browser_principal_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: local-browser-principal-test
local-browser-principal-test: $(BIN_DIR)/local_browser_principal_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/local_browser_principal_test"

$(BIN_DIR)/pi_pair_operations_test: tests/pi_pair_operations_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-operations-test
pi-pair-operations-test: $(BIN_DIR)/pi_pair_operations_test
	$(Q)python3 tests/pairing_provider/generate_rotation_vector.py --check
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_operations_test"

$(BIN_DIR)/pi_pair_recovery_test: tests/pi_pair_recovery_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-recovery-test
pi-pair-recovery-test: $(BIN_DIR)/pi_pair_recovery_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_recovery_test"

$(BIN_DIR)/pi_pair_retirement_key_test: tests/pi_pair_retirement_key_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-retirement-key-test
pi-pair-retirement-key-test: $(BIN_DIR)/pi_pair_retirement_key_test
	$(Q)python3 tests/pairing_provider/generate_retirement_vector.py --check
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_retirement_key_test"

$(BIN_DIR)/pi_pair_admission_test: tests/pi_pair_admission_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-admission-test
pi-pair-admission-test: $(BIN_DIR)/pi_pair_admission_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_admission_test"

$(BIN_DIR)/pi_pair_history_tls_test: tests/pi_pair_history_tls_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-history-tls-test
pi-pair-history-tls-test: $(BIN_DIR)/pi_pair_history_tls_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_history_tls_test"

$(BIN_DIR)/pi_pair_peers_test: tests/pi_pair_peers_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-peers-test
pi-pair-peers-test: $(BIN_DIR)/pi_pair_peers_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_peers_test"

$(BIN_DIR)/pi_pair_window_test: tests/pi_pair_window_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-window-test
pi-pair-window-test: $(BIN_DIR)/pi_pair_window_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_window_test"

$(BIN_DIR)/pi_pair_http_test: tests/pi_pair_http_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-http-test
pi-pair-http-test: $(BIN_DIR)/pi_pair_http_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_http_test"

$(BIN_DIR)/pi_pair_server_test: tests/pi_pair_server_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-server-test
pi-pair-server-test: $(BIN_DIR)/pi_pair_server_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_server_test"

$(BIN_DIR)/pi_pair_client_test: tests/pi_pair_client_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-client-test
pi-pair-client-test: $(BIN_DIR)/pi_pair_client_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_client_test"

$(BIN_DIR)/pi_pair_discovery_test: tests/pi_pair_discovery_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-discovery-test
pi-pair-discovery-test: $(BIN_DIR)/pi_pair_discovery_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_discovery_test"

$(BIN_DIR)/pi_pair_outgoing_rotation_test: tests/pi_pair_outgoing_rotation_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-outgoing-rotation-test
pi-pair-outgoing-rotation-test: $(BIN_DIR)/pi_pair_outgoing_rotation_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_outgoing_rotation_test"

$(BIN_DIR)/pi_pair_guarded_http_test: tests/pi_pair_guarded_http_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-guarded-http-test
pi-pair-guarded-http-test: $(BIN_DIR)/pi_pair_guarded_http_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_guarded_http_test"

$(BIN_DIR)/pi_pair_bootstrap_client_test: tests/pi_pair_bootstrap_client_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-bootstrap-client-test
pi-pair-bootstrap-client-test: $(BIN_DIR)/pi_pair_bootstrap_client_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_bootstrap_client_test"

$(BIN_DIR)/pi_pair_connect_test: tests/pi_pair_connect_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-connect-test
pi-pair-connect-test: $(BIN_DIR)/pi_pair_connect_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_connect_test"

$(BIN_DIR)/pi_pair_usage_test: tests/pi_pair_usage_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-usage-test
pi-pair-usage-test: $(BIN_DIR)/pi_pair_usage_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_usage_test"

$(BIN_DIR)/pi_pair_machine_service_test: tests/pi_pair_machine_service_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-machine-service-test
pi-pair-machine-service-test: $(BIN_DIR)/pi_pair_machine_service_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_machine_service_test"

$(BIN_DIR)/pi_pair_owner_test: tests/pi_pair_owner_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-owner-test
pi-pair-owner-test: $(BIN_DIR)/pi_pair_owner_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_owner_test"

$(BIN_DIR)/pi_pair_service_lifetime_test: tests/pi_pair_service_lifetime_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-service-lifetime-test
pi-pair-service-lifetime-test: $(BIN_DIR)/pi_pair_service_lifetime_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_service_lifetime_test"

$(BIN_DIR)/pi_pair_local_test: tests/pi_pair_local_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
.PHONY: pi-pair-local-test
pi-pair-local-test: $(BIN_DIR)/pi_pair_local_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_local_test"

$(BIN_DIR)/pi_managed_store_test: tests/pi_managed_store_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-managed-store-test: $(BIN_DIR)/pi_managed_store_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_managed_store_test" wtp_integration/contract/control-controller-vectors.json

.PHONY: database-configuration-runtime-test
$(BIN_DIR)/database_configuration_runtime_test: tests/database_configuration_runtime_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
database-configuration-runtime-test: $(BIN_DIR)/database_configuration_runtime_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/database_configuration_runtime_test" ../config/wsprrypi.ini

.PHONY: database-managed-startup-test
$(BIN_DIR)/database_managed_startup_test: tests/database_managed_startup_test.cpp tests/software_child_process.hpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
database-managed-startup-test: $(BIN_DIR)/database_managed_startup_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/database_managed_startup_test" wtp_integration/contract/control-controller-vectors.json
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/database_managed_startup_test" wtp_integration/contract/control-controller-vectors.json --native

.PHONY: database-fresh-startup-test
$(BIN_DIR)/database_fresh_startup_test: tests/database_fresh_startup_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
database-fresh-startup-test: $(BIN_DIR)/database_fresh_startup_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/database_fresh_startup_test" wtp_integration/contract/control-controller-vectors.json

.PHONY: controller-retirement-storage-binding-test
$(BIN_DIR)/controller_retirement_storage_binding_test: tests/controller_retirement_storage_binding_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
controller-retirement-storage-binding-test: $(BIN_DIR)/controller_retirement_storage_binding_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/controller_retirement_storage_binding_test"

.PHONY: database-controller-runtime-test
.PHONY: application-companion-capture-test
$(BIN_DIR)/application_companion_capture_test: tests/application_companion_capture_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
application-companion-capture-test: $(BIN_DIR)/application_companion_capture_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/application_companion_capture_test"

$(BIN_DIR)/database_controller_runtime_test: tests/database_controller_runtime_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
database-controller-runtime-test: $(BIN_DIR)/database_controller_runtime_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/database_controller_runtime_test"

.PHONY: pi-managed-records-test
$(BIN_DIR)/pi_managed_records_test: tests/pi_managed_records_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-managed-records-test: $(BIN_DIR)/pi_managed_records_test
	$(Q)"$(BIN_DIR)/pi_managed_records_test" wtp_integration/contract/control-controller-vectors.json

$(BIN_DIR)/pi_management_journal_test: tests/pi_management_journal_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-management-journal-test: $(BIN_DIR)/pi_management_journal_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_management_journal_test" wtp_integration/contract/control-controller-vectors.json

$(BIN_DIR)/pi_management_anchor_test: tests/pi_management_anchor_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-management-anchor-test: $(BIN_DIR)/pi_management_anchor_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_management_anchor_test"

$(BIN_DIR)/pi_config_persistence_coordinator_test: tests/pi_config_persistence_coordinator_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
pi-config-persistence-coordinator-test: $(BIN_DIR)/pi_config_persistence_coordinator_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_config_persistence_coordinator_test"

$(BIN_DIR)/controller_contract_test: tests/controller_contract_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
controller-contract-test: $(BIN_DIR)/controller_contract_test
	$(Q)"$(BIN_DIR)/controller_contract_test" wtp_integration/contract/control-controller-vectors.json
	$(Q)node ../WsprryPi-UI/tests/controller_contract_test.js

.PHONY: wtp-production-cpp-test wtp-api-cpp-test wtp-config-cpp-test wtp-ui-test wtp-management-ui-test
wtp-ui-test:
	$(Q)node ../WsprryPi-UI/tests/wtp_ui_test.js
wtp-management-ui-test:
	$(Q)node ../WsprryPi-UI/tests/wtp_management_test.js

# Parent network adapters; no physical USB/RF. The explicit interoperability
# runner starts only the pinned Pico server's loopback host adapter.
WTP_NETWORK_BUILD_DIR ?= build/wtp-network
WTP_NETWORK_CXXFLAGS ?= -O2 -g
WTP_NETWORK_LDFLAGS ?=
WTP_NETWORK_FLAGS = -std=c++20 -pthread -Wall -Wextra -Wpedantic -Werror $(WTP_NETWORK_CXXFLAGS) -I. -IWTP-Client/include $(OPENSSL_CFLAGS)
include ../scripts/network_build_rules.mk
.PHONY: wtp-api-test wtp-config-test pico-control-client-build pico-control-interop-test wtp-tls-build pico-https-management-interop-test wtp-network-process-test
wtp-config-cpp-test: $(NETWORK_BIN_DIR)/wtp_configuration_test
	$(Q)cp "$<" "$(WTP_NETWORK_BUILD_DIR)/wtp_configuration_test.tmp"
	$(Q)mv "$(WTP_NETWORK_BUILD_DIR)/wtp_configuration_test.tmp" "$(WTP_NETWORK_BUILD_DIR)/wtp_configuration_test"
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/wtp_configuration_test"

wtp-config-test: wtp-config-cpp-test wtp-management-ui-test
wtp-network-process-test:
	$(Q)python3 -B tests/wtp_network_process_test.py
wtp-api-cpp-test: $(NETWORK_BIN_DIR)/wtp_browser_api_test
	$(Q)cp "$<" "$(WTP_NETWORK_BUILD_DIR)/wtp_browser_api_test.tmp"
	$(Q)mv "$(WTP_NETWORK_BUILD_DIR)/wtp_browser_api_test.tmp" "$(WTP_NETWORK_BUILD_DIR)/wtp_browser_api_test"
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/wtp_browser_api_test"

wtp-api-test: wtp-api-cpp-test wtp-ui-test
wtp-tls-build: wtp-tls-probe-build wtp-network-client-build

.PHONY: wtp-tls-probe-build wtp-network-client-build
wtp-tls-probe-build: $(NETWORK_BIN_DIR)/wtp_tls_probe
	$(Q)pkg-config --atleast-version=3.0 openssl || { echo "OpenSSL 3 development headers/libraries and pkg-config are required" >&2; exit 1; }
	$(Q)cp "$<" "$(WTP_NETWORK_BUILD_DIR)/wtp_tls_probe.tmp"
	$(Q)mv "$(WTP_NETWORK_BUILD_DIR)/wtp_tls_probe.tmp" "$(WTP_NETWORK_BUILD_DIR)/wtp_tls_probe"

wtp-network-client-build:
	$(Q)pkg-config --atleast-version=3.0 openssl
	$(Q)mkdir -p "$(WTP_NETWORK_BUILD_DIR)"
	$(Q)$(MAKE) --no-print-directory -C WTP-Client all BUILD_DIR="$(abspath $(WTP_NETWORK_BUILD_DIR)/client)" CXXFLAGS="$(WTP_NETWORK_CXXFLAGS)"
	$(Q)$(CXX) $(WTP_NETWORK_FLAGS) tests/pico_https_management_interop_test.cpp wtp_integration/tls.cpp wtp_integration/network_http.cpp \
		WSPR-Transmitter/src/execution_plan_compiler.cpp WSPR-Transmitter/src/transmission_controller.cpp \
		$(OPENSSL_LIBS) $(WTP_NETWORK_LDFLAGS) -o "$(WTP_NETWORK_BUILD_DIR)/pico_https_management_interop_test"
pico-control-client-build:
	$(Q)pkg-config --atleast-version=3.0 openssl
	$(Q)mkdir -p "$(WTP_NETWORK_BUILD_DIR)"
	$(Q)$(CXX) $(WTP_NETWORK_FLAGS) -IWSPR-Transmitter/src tests/wtp_configuration_client.cpp wtp_integration/tls.cpp wtp_integration/network_http.cpp \
		wtp_integration/configuration.cpp wtp_integration/controller_contract.cpp wtp_integration/pico_usb_settings.cpp wtp_integration/consumer_radio.cpp wtp_integration/plain_tcp.cpp wtp_integration/browser_api.cpp privileged_network_policy.cpp backend_http_guard.cpp support_request_guard.cpp httplib.cpp \
		$(OPENSSL_LIBS) $(WTP_NETWORK_LDFLAGS) -o "$(WTP_NETWORK_BUILD_DIR)/wtp_configuration_client"
# These fixtures share a CMake tree, credentials and loopback TLS port.
# Serialize an invocation that requests both; parallel recipe builds still use CMake -j.
ifneq ($(filter pico-control-interop-test pico-https-management-interop-test,$(MAKECMDGOALS)),)
.NOTPARALLEL:
endif
pico-control-interop-test: pico-control-client-build wtp-config-test
	$(Q)cmake -S tests/network -B "$(WTP_NETWORK_BUILD_DIR)" -DPICO_SOURCE="$(abspath $(PICO_SOURCE))" -DMBEDTLS_SOURCE="$(abspath $(MBEDTLS_SOURCE))"
	$(Q)cmake --build "$(WTP_NETWORK_BUILD_DIR)" -j 2
	$(Q)node ../WsprryPi-UI/tests/wtp_configuration_interop_test.js "$(abspath $(WTP_NETWORK_BUILD_DIR))"
	$(Q)node ../WsprryPi-UI/tests/wtp_progress_interop_test.js "$(abspath $(WTP_NETWORK_BUILD_DIR))"
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/consumer_interop" "$(abspath $(PICO_SOURCE))/engineering/protocol/Consumer-Radio-v1-vectors.json"
	$(Q)$(MAKE) --no-print-directory $(BIN_DIR)/wspr_scheduler_frequency_export BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./$(BIN_DIR)/wspr_scheduler_frequency_export --export-native-tests "$(WTP_NETWORK_BUILD_DIR)/scheduler-frequency.jsonl"
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/consumer_interop" --scheduler-frequency "$(WTP_NETWORK_BUILD_DIR)/scheduler-frequency.jsonl"
	$(Q)node ../WsprryPi-UI/tests/wtp_resources_interop_test.js "$(abspath $(WTP_NETWORK_BUILD_DIR))"
	$(Q)node ../WsprryPi-UI/tests/wtp_amplifier_timing_interop_test.js "$(abspath $(WTP_NETWORK_BUILD_DIR))"
	$(Q)node ../WsprryPi-UI/tests/wtp_gpio_drive_interop_test.js "$(abspath $(WTP_NETWORK_BUILD_DIR))"
	$(Q)node tests/network/consumer_browser_interop.js "$(abspath $(WTP_NETWORK_BUILD_DIR))" "$(abspath $(PICO_SOURCE))"
	$(Q)$(MAKE) --no-print-directory $(BIN_DIR)/pico_management_pair_client BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
	$(Q)node ../WsprryPi-UI/tests/pico_management_transport_pair_test.js "$(abspath $(WTP_NETWORK_BUILD_DIR))" "$(abspath $(BIN_DIR)/pico_management_pair_client)"
	$(Q)$(MAKE) --no-print-directory $(BIN_DIR)/pico_controller_pair_fixture BACKENDS=simulated ANCILLARY_GPIO=0 SUDO=
	$(Q)node ../WsprryPi-UI/tests/pico_controller_http_pair_test.js "$(abspath $(WTP_NETWORK_BUILD_DIR))" "$(abspath $(BIN_DIR)/pico_controller_pair_fixture)"
pico-https-management-interop-test: wtp-tls-build wtp-api-test wtp-network-process-test
	$(Q)cmake -S tests/network -B "$(WTP_NETWORK_BUILD_DIR)" -DPICO_SOURCE="$(abspath $(PICO_SOURCE))" -DMBEDTLS_SOURCE="$(abspath $(MBEDTLS_SOURCE))"
	$(Q)cmake --build "$(WTP_NETWORK_BUILD_DIR)" -j 4
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/pico_execution_fixture_clock_test"
	$(Q)python3 -B tests/pico_https_management_interop_test.py "$(abspath $(WTP_NETWORK_BUILD_DIR))"

.PHONY: wtp-tls-test
wtp-tls-test: wtp-tls-probe-build $(NETWORK_BIN_DIR)/wtp_tls_contract_test
	$(Q)cp "$(NETWORK_BIN_DIR)/wtp_tls_contract_test" "$(WTP_NETWORK_BUILD_DIR)/wtp_tls_contract_test.tmp"
	$(Q)mv "$(WTP_NETWORK_BUILD_DIR)/wtp_tls_contract_test.tmp" "$(WTP_NETWORK_BUILD_DIR)/wtp_tls_contract_test"
	$(Q)python3 -B tests/wtp_tls_test.py "$(abspath $(WTP_NETWORK_BUILD_DIR))"

.PHONY: native-clock-test
$(BIN_DIR)/native_clock_test: tests/native_clock_test.cpp native_clock.hpp native_calendar_time.hpp native_windows_file.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -pthread -I. $(if $(filter Windows_NT,$(OS)),-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00,) $(filter %.cpp,$^) -o $@ $(if $(filter Windows_NT,$(OS)),-lmincore,)
native-clock-test: $(BIN_DIR)/native_clock_test
	$(Q)$(BIN_DIR)/native_clock_test

.PHONY: native-socket-test
$(BIN_DIR)/native_socket_test: tests/native_socket_test.cpp wtp_integration/native_socket.hpp
	$(Q)mkdir -p "$(dir $@)"
	$(Q)$(CXX) -std=c++20 -pthread -I. "$<" -o "$@" $(if $(filter Windows_NT,$(OS)),-lws2_32,)
native-socket-test: $(BIN_DIR)/native_socket_test
	$(Q)"$<"

.PHONY: wtp-plain-tcp-test
wtp-plain-tcp-test: $(NETWORK_BIN_DIR)/wtp_plain_tcp_test
	$(Q)cp "$<" "$(WTP_NETWORK_BUILD_DIR)/wtp_plain_tcp_test.tmp"
	$(Q)mv "$(WTP_NETWORK_BUILD_DIR)/wtp_plain_tcp_test.tmp" "$(WTP_NETWORK_BUILD_DIR)/wtp_plain_tcp_test"
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/wtp_plain_tcp_test"

.PHONY: pi-network-runtime-regression-test
# Runtime certificate-rotation cases consume the fixtures generated by TLS tests.
pi-network-runtime-regression-test: wtp-tls-test wtp-plain-tcp-test $(BIN_DIR)/wtp_production_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./$(BIN_DIR)/wtp_production_test "$(abspath $(WTP_NETWORK_BUILD_DIR)/tls-fixtures)"

# Header ABI changes must invalidate every selected object configuration.
.PHONY: object-dependency-test
object-dependency-test:
	$(Q)python3 ../scripts/tests/object_dependency_make_regression_test.py
	$(Q)python3 ../scripts/tests/wtp_server_make_regression_test.py

.PHONY: wtp-fleet-test
$(BIN_DIR)/wtp_fleet_test: tests/wtp_fleet_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
wtp-fleet-test: $(BIN_DIR)/wtp_fleet_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./$(BIN_DIR)/wtp_fleet_test

.PHONY: wtp-pi-config-transaction-test
$(BIN_DIR)/wtp_pi_config_transaction_test: tests/wtp_pi_config_transaction_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
wtp-pi-config-transaction-test: $(BIN_DIR)/wtp_pi_config_transaction_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./$(BIN_DIR)/wtp_pi_config_transaction_test

.PHONY: si5351-inventory-process-test si5351-inventory-isolation-test
si5351-inventory-process-test:
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++17 -Wall -Wextra -Werror -pthread -I. -DWSPRRYPI_BACKEND_SI5351=0 tests/si5351_inventory_process_test.cpp si5351_inventory_process.cpp -o $(BIN_DIR)/si5351_inventory_process_test
	$(Q)./$(BIN_DIR)/si5351_inventory_process_test

$(BIN_DIR)/si5351_inventory_isolation_test: tests/si5351_inventory_isolation_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
si5351-inventory-isolation-test: $(BIN_DIR)/si5351_inventory_isolation_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./$(BIN_DIR)/si5351_inventory_isolation_test

# Parent lifecycle integration only. The fixture suppresses hardware execution
# and injects typed events/failures after real parent planning/commitment.
.PHONY: test-tone-lifecycle-test
$(BIN_DIR)/test_tone_lifecycle_test: tests/test_tone_lifecycle_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)

test-tone-lifecycle-test: $(BIN_DIR)/test_tone_lifecycle_test
	$(Q)python3 -c 'import subprocess; subprocess.run(["./$(BIN_DIR)/test_tone_lifecycle_test"], check=True, timeout=30)'

.PHONY: pico-usb-management-contract-test
pico-usb-management-contract-test: $(NETWORK_BIN_DIR)/wtp_usb_management_test
	$(Q)cp "$<" "$(WTP_NETWORK_BUILD_DIR)/wtp_usb_management_test.tmp"
	$(Q)mv "$(WTP_NETWORK_BUILD_DIR)/wtp_usb_management_test.tmp" "$(WTP_NETWORK_BUILD_DIR)/wtp_usb_management_test"
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/wtp_usb_management_test"

WTP_USB_PAIR_SOURCES := tests/pico_usb_control_client.cpp wtp_integration/usb_cdc.cpp $(NATIVE_CDC_SOURCES) wtp_integration/usb_management.cpp wtp_integration/configuration.cpp wtp_integration/controller_contract.cpp wtp_integration/pico_usb_settings.cpp wtp_integration/browser_api.cpp wtp_integration/consumer_radio.cpp wtp_integration/plain_tcp.cpp wtp_integration/network_http.cpp wtp_integration/tls.cpp privileged_network_policy.cpp backend_http_guard.cpp support_request_guard.cpp httplib.cpp
WTP_USB_PAIR_OBJECTS := $(addprefix $(WTP_NETWORK_BUILD_DIR)/usb-pair/,$(WTP_USB_PAIR_SOURCES:.cpp=.o))
.PHONY: pico-usb-control-client-build
pico-usb-control-client-build: $(WTP_NETWORK_BUILD_DIR)/pico_usb_control_client
$(WTP_NETWORK_BUILD_DIR)/pico_usb_control_client: $(WTP_USB_PAIR_OBJECTS)
	$(Q)$(CXX) $(WTP_NETWORK_FLAGS) $^ $(OPENSSL_LIBS) $(NATIVE_USB_LIBS) $(WTP_NETWORK_LDFLAGS) -o "$@"
$(WTP_NETWORK_BUILD_DIR)/usb-pair/%.o: %.cpp
	$(Q)mkdir -p "$(dir $@)"
	$(Q)$(CXX) $(WTP_NETWORK_FLAGS) -MMD -MP -c "$<" -o "$@"
-include $(WTP_USB_PAIR_OBJECTS:.o=.d)

.PHONY: pico-usb-installer-test pico-usb-installer-cpp-test pico-usb-installer-offline-test
pico-usb-installer-cpp-test:
	$(Q)mkdir -p "$(WTP_NETWORK_BUILD_DIR)"
	$(Q)$(CXX) $(WTP_NETWORK_FLAGS) tests/pico_usb_installer_test.cpp -o "$(WTP_NETWORK_BUILD_DIR)/pico_usb_installer_test"
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/pico_usb_installer_test"

pico-usb-installer-test: pico-usb-installer-offline-test
	$(Q)node ../WsprryPi-UI/tests/wtp_firmware_test.js

pico-usb-installer-offline-test: pico-usb-installer-cpp-test
	$(Q)python3 -B ../scripts/tests/pico_usb_deploy_test.py
	$(Q)python3 -B ../scripts/tests/pico_usb_bundle_test.py

.PHONY: wspr-plan-placement-test
$(BIN_DIR)/wspr_plan_placement_test $(BIN_DIR)/wspr_scheduler_frequency_export: tests/wspr_plan_placement_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) $^ -o $@ $(LDFLAGS)
wspr-plan-placement-test: $(BIN_DIR)/wspr_plan_placement_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 ./$(BIN_DIR)/wspr_plan_placement_test

# Hardware-free bounded USB parser and Linux collection-policy persistence.
.PHONY: usb-logging-test
usb-logging-test:
	$(Q)mkdir -p build/usb-logging
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -pthread -I. -IWTP-Client/include -DLIB_JOURNALD=0 $(OPENSSL_CFLAGS) -IWTP-Server/include tests/usb_logging_test.cpp WTP-Server/src/sha256.cpp wtp_integration/usb_logging.cpp wtp_integration/usb_discovery.cpp wtp_integration/usb_cdc.cpp $(NATIVE_CDC_SOURCES) wtp_integration/catalog.cpp wtp_endpoint/identity.cpp $(OPENSSL_LIBS) $(NATIVE_USB_LIBS) -o build/usb-logging/usb_logging_test
	$(Q)build/usb-logging/usb_logging_test

# Dedicated worker is bundled into the verified executable for source and
# precompiled installation, then extracted as a separate unprivileged image.
UDP_WORKER_BIN := build/udp-logging/wsprrypi-log
UDP_WORKER_HEADER := $(BUILD_METADATA_DIR)/udp_worker_image.inc
UDP_WORKER_INPUTS := udp_logging/main.cpp udp_logging/network_policy.hpp udp_logging/resource_limits.hpp udp_logging/contract.hpp udp_logging/io.hpp udp_logging/udp_wire.hpp udp_logging/udp_event_policy.hpp json.hpp
.PHONY: udp-logging-worker udp-logging-test
$(UDP_WORKER_BIN): $(UDP_WORKER_INPUTS)
	$(Q)mkdir -p build/udp-logging
	$(Q)$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -pthread -I. udp_logging/main.cpp -lsystemd -lcrypto -o $@
udp-logging-worker: $(UDP_WORKER_BIN)
ifeq ($(HAVE_JOURNALD),1)
$(UDP_WORKER_HEADER): $(UDP_WORKER_BIN) ../scripts/embed_udp_worker.py
	$(Q)python3 ../scripts/embed_udp_worker.py $(UDP_WORKER_BIN) $@
else
$(UDP_WORKER_HEADER): ../scripts/embed_udp_worker.py
	$(Q)python3 ../scripts/embed_udp_worker.py unavailable $@
endif
$(OBJ_DIR_RELEASE)/./udp_worker_image.o $(OBJ_DIR_DEBUG)/./udp_worker_image.o: $(UDP_WORKER_HEADER)

# Recovery encryption is bundled with the verified application artifact. Its
# maintained age/Go inputs are pinned independently from support-intake crypto.
RECOVERY_AGE_ROOT := $(abspath build/recovery-age)
RECOVERY_AGE_HEADER := $(BUILD_METADATA_DIR)/recovery_age_image.inc
RECOVERY_AGE_GO ?=
RECOVERY_AGE_BUILD = python3 ../scripts/build_recovery_age.py --root "$(RECOVERY_AGE_ROOT)" --compiler "$(CXX)" $(if $(RECOVERY_AGE_GO),--go "$(RECOVERY_AGE_GO)")
$(RECOVERY_AGE_HEADER): $(wildcard application_recovery/age_helper/*.go) application_recovery/age_helper/go.mod application_recovery/age_helper/go.sum ../scripts/recovery_age_provider.json ../scripts/build_recovery_age.py
	$(Q)$(RECOVERY_AGE_BUILD) --embed "$@" >/dev/null
$(OBJ_DIR_RELEASE)/application_recovery/helper_image.o $(OBJ_DIR_DEBUG)/application_recovery/helper_image.o: $(RECOVERY_AGE_HEADER)
.PHONY: recovery-age-unit-test recovery-archive-test
recovery-age-unit-test:
	$(Q)$(RECOVERY_AGE_BUILD) --test >/dev/null
recovery-archive-test:
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_RECOVERY_AGE_HELPER="$$helper" python3 -B ../scripts/tests/recovery_archive_test.py
.PHONY: recovery-capture-backend-test
recovery-capture-backend-test: $(BIN_DIR)/browser_session_backend_fixture
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/browser_session_backend_test.py $(BIN_DIR)/browser_session_backend_fixture "$$helper"
udp-logging-test:
	$(Q)mkdir -p build/udp-logging
	$(Q)$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic -Werror -I. -IWTP-Client/include tests/udp_logging_test.cpp -o build/udp-logging/udp_logging_test
	$(Q)build/udp-logging/udp_logging_test

# Actual application and finite read-only load, built without physical backends.
.PHONY: production-browser-regression-test tls-observer-test
TEST_NETNS_RUNNER ?= unshare --net
production-browser-regression-test: debug $(BIN_DIR)/pi_managed_main_fixture
	$(Q)python3 -B tests/production_browser_load_test.py
	$(Q)$(TEST_NETNS_RUNNER) python3 -B tests/production_ini_roundtrip_test.py "$(abspath $(PROFILE_DEBUG_BIN))" --fixture "$(abspath $(BIN_DIR)/pi_managed_main_fixture)"

tls-observer-test:
	$(Q)mkdir -p "$(WTP_NETWORK_BUILD_DIR)"
	$(Q)$(CC) -std=c11 -shared -fPIC -Wall -Wextra -Werror tests/network/tls_observer.c -o "$(WTP_NETWORK_BUILD_DIR)/tls_observer.so" $(OPENSSL_CFLAGS) $(OPENSSL_LIBS) -ldl -pthread
	$(Q)python3 -B tests/tls_observer_test.py --observer "$(abspath $(WTP_NETWORK_BUILD_DIR))/tls_observer.so"

.PHONY: linux-i2c-identity-test
linux-i2c-identity-test:
	$(MAKE) -C WSPR-Transmitter/src linux-i2c-identity-test

.PHONY: linux-i2c-identity-config-test
linux-i2c-identity-config-test: $(BIN_DIR)/$(SEMANTICS_TEST_OUT)
	$(Q)./$(BIN_DIR)/$(SEMANTICS_TEST_OUT) --linux-i2c-identity-config-only

.PHONY: mcp2221a-transport-test
mcp2221a-transport-test:
	$(MAKE) -C WSPR-Transmitter/src mcp2221a-transport-test SI5351_TRANSPORT=$(SI5351_TRANSPORT)


.PHONY: ci-focused-state-test ci-network-policy-test ci-support-lifecycle-test ci-installer-offline-test
# The full semantics suite owns the shared state and recovery tests.
ci-focused-state-test: guarded-mode-change-persistence-test band-gpio-default-convergence-test band-gpio-rotation-test selector-shutdown-cleanup-test monitor-file-regression-test gpio-line-resolver-test non-wspr-repeat-policy-test
ci-network-policy-test: support-request-guard-test privileged-network-policy-test backend-http-guard-test websocket-upgrade-guard-test privileged-network-transaction-test privileged-network-runtime-test privileged-network-admin-test
ci-support-lifecycle-test: support-bundle-result-validator-test support-bundle-runtime-test support-bundle-download-file-test support-bundle-checksum-file-test support-bundle-archive-digest-test support-bundle-download-preparation-test support-bundle-job-directory-remover-test support-bundle-startup-cleanup-test
ci-installer-offline-test: application-state-installer-test recovery-runtime-package-test support-bundle-runtime-installer-test pico-usb-installer-offline-test
	$(Q)python3 -B ../scripts/tests/apache_privileged_network_policy_renderer_test.py

.PHONY: application-state-installer-test
application-state-installer-test:
	$(Q)python3 -B ../scripts/tests/application_state_install_test.py
	$(Q)python3 -B ../scripts/tests/application_state_install_shell_test.py
	$(Q)python3 -B ../scripts/tests/browser_service_install_test.py

.PHONY: gpio-line-resolver-test non-wspr-repeat-policy-test


.PHONY: wtp-fleet-operations-driver-build
$(BIN_DIR)/wtp_fleet_operations_driver: tests/fleet_retirement_pair_driver.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
wtp-fleet-operations-driver-build: $(BIN_DIR)/wtp_fleet_operations_driver

# Pure geometry: no application, device, networking, or clock acquisition.
.PHONY: solar-geometry-test
$(BIN_DIR)/solar_geometry_test: tests/solar_geometry_test.cpp solar_geometry.cpp solar_geometry.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. tests/solar_geometry_test.cpp solar_geometry.cpp -o $@
solar-geometry-test: $(BIN_DIR)/solar_geometry_test
	$(Q)$(BIN_DIR)/solar_geometry_test

.PHONY: solar-policy-test
$(BIN_DIR)/solar_policy_test: tests/solar_policy_test.cpp solar_policy.cpp solar_policy.hpp solar_geometry.cpp solar_geometry.hpp config_types.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. -IWSPR-Reference/src/wspr tests/solar_policy_test.cpp solar_policy.cpp solar_geometry.cpp -o $@
solar-policy-test: $(BIN_DIR)/solar_policy_test
	$(Q)$(BIN_DIR)/solar_policy_test

.PHONY: solar-geometry-pair-test
solar-geometry-pair-test:
	$(Q)test -n "$(PICO_SOURCE)" -a -n "$(MBEDTLS_SOURCE)"
	$(Q)cmake -S tests/network -B "$(WTP_NETWORK_BUILD_DIR)" -DPICO_SOURCE="$(abspath $(PICO_SOURCE))" -DMBEDTLS_SOURCE="$(abspath $(MBEDTLS_SOURCE))"
	$(Q)cmake --build "$(WTP_NETWORK_BUILD_DIR)" --target pi_solar_geometry_pair -j4
	$(Q)"$(WTP_NETWORK_BUILD_DIR)/pi_solar_geometry_pair"

.PHONY: recurrence-authority-record-test
.PHONY: solar-clock-test
.PHONY: native-recurrence-test
.PHONY: wtp-retirement-config-test
$(BIN_DIR)/wtp_retirement_config_test: tests/wtp_retirement_config_test.cpp wtp_retirement.hpp config_types.hpp wtp_settings.hpp wtp_settings_json.hpp wtp_server_settings.hpp wtp_server_settings_json.hpp wtp_integration/identity.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Wpedantic -Werror -I. -IWSPR-Reference/src/wspr tests/wtp_retirement_config_test.cpp -o $@
wtp-retirement-config-test: $(BIN_DIR)/wtp_retirement_config_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 $(BIN_DIR)/wtp_retirement_config_test
$(BIN_DIR)/native_recurrence_test: tests/native_recurrence_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
native-recurrence-test: $(BIN_DIR)/native_recurrence_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 $(BIN_DIR)/native_recurrence_test
.PHONY: native-recurrence-main-test
native-recurrence-main-test: $(BIN_DIR)/native_recurrence_main_fixture
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 tests/native_recurrence_main_test.py --fixture $(BIN_DIR)/native_recurrence_main_fixture
.PHONY: recurrence-progress-test
$(BIN_DIR)/recurrence_progress_test: tests/recurrence_progress_test.cpp recurrence_progress.cpp recurrence_progress.hpp wtp_integration/pi_management_journal.cpp wtp_integration/pi_management_journal.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. $(OPENSSL_CFLAGS) tests/recurrence_progress_test.cpp recurrence_progress.cpp wtp_integration/pi_management_journal.cpp -o $@ $(OPENSSL_LIBS)
recurrence-progress-test: $(BIN_DIR)/recurrence_progress_test
	$(Q)$(BIN_DIR)/recurrence_progress_test

$(BIN_DIR)/solar_clock_test: tests/solar_clock_test.cpp solar_clock.cpp solar_clock.hpp native_clock.hpp solar_policy.cpp solar_policy.hpp solar_geometry.cpp solar_geometry.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. -IWSPR-Reference/src/wspr $(if $(filter Windows_NT,$(OS)),-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00,) tests/solar_clock_test.cpp solar_clock.cpp solar_policy.cpp solar_geometry.cpp -o $@ $(if $(filter Windows_NT,$(OS)),-lmincore,)
solar-clock-test: $(BIN_DIR)/solar_clock_test
	$(Q)$(BIN_DIR)/solar_clock_test

$(BIN_DIR)/recurrence_authority_record_test: tests/recurrence_authority_record_test.cpp recurrence_authority.cpp recurrence_authority.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. tests/recurrence_authority_record_test.cpp recurrence_authority.cpp -o $@
recurrence-authority-record-test: $(BIN_DIR)/recurrence_authority_record_test
	$(Q)$(BIN_DIR)/recurrence_authority_record_test

.PHONY: scheduled-launch-test
scheduled-launch-test:
	$(Q)$(MAKE) -C WSPR-Transmitter/src scheduled-launch-test BACKENDS=simulated SUDO=

.PHONY: pi-pair-local-http-test
pi-pair-local-http-test: $(BIN_DIR)/pi_pair_local_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_local_test" --http

.PHONY: pi-pair-runtime-test
pi-pair-runtime-test: $(BIN_DIR)/pi_pair_local_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/pi_pair_local_test" --runtime

# Static Botan code is distributed with its upstream notice and exact pin.
.PHONY: pairing-notices
PAIRING_NOTICE_DIR := $(if $(filter controller,$(BUILD_PROFILE)),$(PROFILE_BIN_DIR),$(BIN_DIR))
pairing-notices:
	$(Q)mkdir -p "$(PAIRING_NOTICE_DIR)"
	$(Q)cp ../docs/licenses/botan/BSD-2-Clause.txt "$(PAIRING_NOTICE_DIR)/BOTAN-LICENSE.txt"
	$(Q)cp ../scripts/pairing_provider.json "$(PAIRING_NOTICE_DIR)/PAIRING-PROVIDER.json"

.PHONY: install-pairing-notices
install-pairing-notices: pairing-notices
	$(Q)$(SUDO) install -d "$(PREFIX)/../share/licenses/wsprrypi"
	$(Q)$(SUDO) install -m 644 "$(BIN_DIR)/BOTAN-LICENSE.txt" "$(PREFIX)/../share/licenses/wsprrypi/BOTAN-LICENSE.txt"

.PHONY: restored-generation-test
$(BIN_DIR)/restored_generation_test: tests/restored_generation_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $^ -o $@ $(LDFLAGS)
restored-generation-test: $(BIN_DIR)/restored_generation_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 $(BIN_DIR)/restored_generation_test

.PHONY: recovery-publication-test
recovery-publication-test:
	$(Q)python3 -B ../scripts/tests/recovery_publication_test.py

.PHONY: recovery-worker-process-test
$(BIN_DIR)/recovery_worker_process_test: tests/recovery_worker_process_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
recovery-worker-process-test: $(BIN_DIR)/recovery_worker_process_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && $(BIN_DIR)/recovery_worker_process_test /usr/bin/python3 "$$helper"

.PHONY: recovery-jobs-test
recovery-jobs-test:
	$(Q)python3 -B ../scripts/tests/recovery_jobs_test.py

.PHONY: recovery-browser-admission-test
$(BIN_DIR)/recovery_browser_admission_test: tests/recovery_browser_admission_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
recovery-browser-admission-test: $(BIN_DIR)/recovery_browser_admission_test
	$(Q)$(BIN_DIR)/recovery_browser_admission_test

$(BIN_DIR)/recovery_maintenance_fixture: tests/recovery_maintenance_fixture.cpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)

$(BIN_DIR)/recovery_native_owner_fixture: tests/recovery_native_owner_fixture.cpp tests/recovery_maintenance_fixture.cpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests tests/recovery_native_owner_fixture.cpp $(filter %.o,$^) -o $@ $(LDFLAGS)

.PHONY: recovery-native-owner-controller-test recovery-native-owner-move-test recovery-native-owner-crash-test
.PHONY: controller-native-accounts-fixture-build
controller-native-accounts-fixture-build: $(PROFILE_BIN_DIR)/controller_native_accounts_fixture
$(PROFILE_BIN_DIR)/controller_native_accounts_fixture: tests/controller_native_accounts_fixture.cpp $(OBJ_DIR_DEBUG)/tests/pi_managed_production_main.o $(SEMANTICS_TEST_DEPS)
	$(Q)test "$(BUILD_PROFILE)" = controller
	$(Q)mkdir -p $(PROFILE_BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)

recovery-native-owner-controller-test: $(BIN_DIR)/recovery_native_owner_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_native_owner_fixture "$$helper" --controller

recovery-native-owner-move-test: $(BIN_DIR)/recovery_native_owner_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_native_owner_fixture "$$helper" --controller-move

recovery-native-owner-crash-test: $(BIN_DIR)/recovery_native_owner_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && for phase in stopping_backend retaining_evidence publishing_generation starting_backend verifying_backend complete; do WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_native_owner_fixture "$$helper" "--controller-crash=$$phase" || exit 1; done

.PHONY: recovery-maintenance-test
recovery-maintenance-test: $(BIN_DIR)/recovery_maintenance_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper"

.PHONY: recovery-maintenance-browser-test recovery-maintenance-crash-test recovery-runtime-package-test
recovery-maintenance-browser-test: $(BIN_DIR)/recovery_maintenance_fixture
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper" --browser
recovery-maintenance-crash-test: $(BIN_DIR)/recovery_maintenance_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && for phase in stopping_backend retaining_evidence publishing_generation starting_backend verifying_backend complete; do WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper" "$$phase" || exit 1; done
recovery-runtime-package-test:
	$(Q)python3 -B ../scripts/tests/recovery_runtime_install_test.py
	$(Q)python3 -B ../scripts/tests/recovery_proxy_test.py
	$(Q)python3 -B ../scripts/tests/browser_service_install_test.py

.PHONY: recovery-maintenance-controller-test recovery-maintenance-move-test
recovery-maintenance-controller-test: $(BIN_DIR)/recovery_maintenance_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper" --controller
recovery-maintenance-move-test: $(BIN_DIR)/recovery_maintenance_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper" --move

.PHONY: recovery-maintenance-roundtrip-test recovery-maintenance-listener-override-test
recovery-maintenance-roundtrip-test: $(BIN_DIR)/recovery_maintenance_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper" --roundtrip
recovery-maintenance-listener-override-test: $(BIN_DIR)/recovery_maintenance_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper" --listener-override

.PHONY: diagnostic-export-test
$(BIN_DIR)/diagnostic_export_test: tests/diagnostic_export_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
diagnostic-export-test: $(BIN_DIR)/diagnostic_export_test
	$(Q)$(BIN_DIR)/diagnostic_export_test

.PHONY: recovery-destination-settings-test
$(BIN_DIR)/recovery_destination_settings_test: tests/recovery_destination_settings_test.cpp application_recovery/destination_settings.cpp application_recovery/destination_settings.hpp application_recovery/definition_adaptation.hpp application_storage/persistent_configuration.cpp wtp_integration/controller_contract.cpp wtp_integration/browser_api.cpp application_storage/sqlite_state_store.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp,$^) -o $@ $(LDFLAGS)
recovery-destination-settings-test: $(BIN_DIR)/recovery_destination_settings_test
	$(Q)$(BIN_DIR)/recovery_destination_settings_test

.PHONY: recovery-evidence-test
$(BIN_DIR)/recovery_evidence_test: tests/recovery_evidence_test.cpp application_recovery/evidence.cpp wtp_integration/pi_management_journal.cpp application_control/local_transport.cpp wtp_integration/browser_api.cpp wtp_integration/controller_contract.cpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp,$^) -o $@ $(LDFLAGS)
recovery-evidence-test: $(BIN_DIR)/recovery_evidence_test
	$(Q)python3 -B ../scripts/tests/recovery_evidence_test.py $(BIN_DIR)/recovery_evidence_test

# Serialize the actual-process matrix because fixtures reserve the same bounded
# listener range. These checks never invoke installed services or hardware.
RECOVERY_INTEGRATION_TESTS := recovery-state-owner-test recovery-worker-process-test recovery-native-owner-controller-test recovery-native-owner-move-test recovery-native-owner-crash-test recovery-maintenance-controller-test recovery-maintenance-move-test recovery-maintenance-roundtrip-test recovery-maintenance-listener-override-test recovery-maintenance-history-test recovery-maintenance-crash-test
.PHONY: recovery-integration-test recovery-integration-after-portable-test
recovery-integration-test:
	$(Q)$(MAKE) --no-print-directory -j1 $(RECOVERY_INTEGRATION_TESTS)

# Portable semantics already exercises the generic recovery worker.
recovery-integration-after-portable-test:
	$(Q)$(MAKE) --no-print-directory -j1 $(filter-out recovery-worker-process-test,$(RECOVERY_INTEGRATION_TESTS))

.PHONY: recovery-maintenance-controller-browser-test
recovery-maintenance-controller-browser-test: $(BIN_DIR)/recovery_maintenance_fixture
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper" --controller-browser

.PHONY: recovery-maintenance-history-test
recovery-maintenance-history-test: $(BIN_DIR)/recovery_maintenance_fixture $(BIN_DIR)/recovery_evidence_test
	$(Q)helper="$$( $(RECOVERY_AGE_BUILD) )" && WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/recovery_maintenance_test.py $(BIN_DIR)/recovery_maintenance_fixture "$$helper" --history

.PHONY: recovery-context-busy-test
$(BIN_DIR)/recovery_context_busy_test: tests/recovery_context_busy_test.cpp $(SEMANTICS_TEST_DEPS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CXX_DEBUG_FLAGS) -Itests $(filter %.cpp %.o,$^) -o $@ $(LDFLAGS)
recovery-context-busy-test: $(BIN_DIR)/recovery_context_busy_test
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 "$(BIN_DIR)/recovery_context_busy_test"

.PHONY: native-controller-owners-test
$(BIN_DIR)/native_controller_owners_test: tests/native_controller_owners_test.cpp native_controller_owners.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -I. $< -o $@
native-controller-owners-test: $(BIN_DIR)/native_controller_owners_test
	$(Q)"$(BIN_DIR)/native_controller_owners_test"

.PHONY: native-controller-identity-runtime-test
native-controller-identity-runtime-test: $(CONTROLLER_PRODUCTION_FIXTURE)
	$(Q)python3 -B tests/native_controller_identity_runtime_test.py "$(CONTROLLER_PRODUCTION_FIXTURE)"

.PHONY: application-monitor-boundary-test
application-monitor-boundary-test: debug
	$(Q)python3 -B ../scripts/tests/application_monitor_audit.py "$(PROFILE_DEBUG_BIN)"

.PHONY: recovery-state-owner-test
recovery-state-owner-test:
	$(Q)python3 -B ../scripts/tests/recovery_state_owner_test.py
	$(Q)python3 -B ../scripts/tests/recovery_windows_service_policy_test.py

.PHONY: wtp-host-reference-test
$(BIN_DIR)/wtp_host_references_test: tests/wtp_host_references_test.cpp host_references.hpp wtp_settings.hpp wtp_settings_json.hpp application_recovery/definition_adaptation.hpp
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) -std=c++20 -Wall -Wextra -Werror -UNDEBUG -I. $< -o $@
wtp-host-reference-test: $(BIN_DIR)/wtp_host_references_test
	$(Q)"$(BIN_DIR)/wtp_host_references_test"

# The privileged bench owner is a separate Linux executable. It links only
# pure planning, its scoped native adapter, and the mailbox dependency.
GPIO_QUALIFICATION_COMPONENT = WSPR-Transmitter/src
GPIO_QUALIFICATION_SOURCES = $(wildcard qualification/*.cpp) \
    $(GPIO_QUALIFICATION_COMPONENT)/bcm2835_qualification_plan.cpp \
    $(GPIO_QUALIFICATION_COMPONENT)/bcm2835_qualification_hardware.cpp \
    $(GPIO_QUALIFICATION_COMPONENT)/gpio_frequency_plan.cpp \
    $(GPIO_QUALIFICATION_COMPONENT)/legacy_gpio_clock_model.cpp \
    system_clock_frequency_estimate.cpp Mailbox/src/mailbox.cpp Mailbox/src/mailbox_revision.cpp \
    WTP-Server/src/sha256.cpp
GPIO_QUALIFICATION_HEADERS = $(wildcard qualification/*.hpp) \
    $(wildcard $(GPIO_QUALIFICATION_COMPONENT)/*qualification*.hpp) \
    $(GPIO_QUALIFICATION_COMPONENT)/gpio_frequency_plan.hpp

.PHONY: gpio-qualification-owner gpio-qualification-owner-test
gpio-qualification-owner: $(BIN_DIR)/wsprrypi-gpio-qualification-owner

$(BIN_DIR)/wsprrypi-gpio-qualification-owner: $(GPIO_QUALIFICATION_SOURCES) $(GPIO_QUALIFICATION_HEADERS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++20 -Wall -Wextra -Werror -O2 -pthread \
		-I. -IWTP-Server/include -IChipset-Offsets/src -IMailbox/src -IPPM-Manager/src \
		-DGPIO_QUALIFICATION_REVISION='"$(shell git rev-parse HEAD)"' $(GPIO_QUALIFICATION_SOURCES) -o $@

$(BIN_DIR)/gpio_qualification_owner_fixture: $(GPIO_QUALIFICATION_SOURCES) $(GPIO_QUALIFICATION_HEADERS)
	$(Q)mkdir -p $(BIN_DIR)
	$(Q)$(CXX) $(CPPFLAGS) $(CXXFLAGS) -std=c++20 -Wall -Wextra -Werror -O2 -pthread \
		-I. -IWTP-Server/include -IChipset-Offsets/src -IMailbox/src -IPPM-Manager/src \
		-DGPIO_QUALIFICATION_TEST -DGPIO_QUALIFICATION_REVISION='"$(shell git rev-parse HEAD)"' \
		$(GPIO_QUALIFICATION_SOURCES) -o $@

gpio-qualification-owner-test: $(BIN_DIR)/gpio_qualification_owner_fixture
	$(Q)WSPRRYPI_DISABLE_HARDWARE_ACCESS=1 python3 -B tests/gpio_qualification_owner_test.py "$<"
