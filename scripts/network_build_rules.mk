# These fixtures have the same compilation flags and include search order.
# Configuration-client, standalone WTP archives and USB-pair objects stay separate.
shell_quote = '$(subst ','"'"',$(1))'
NETWORK_BUILD_RULES := ../scripts/network_build_rules.mk
NETWORK_SYSTEM_LIBRARIES := $(wildcard $(shell pkg-config --variable=libdir openssl 2>/dev/null)/libssl.* $(shell pkg-config --variable=libdir openssl 2>/dev/null)/libcrypto.*)
ifeq ($(strip $(filter-out clean help macros build-job-count ci-build-budget,$(MAKECMDGOALS))),)
ifneq ($(strip $(MAKECMDGOALS)),)
NETWORK_BUILD_ID := nonbuild
NETWORK_COMPILE_ID := nonbuild
endif
endif
ifeq ($(NETWORK_BUILD_ID),)
NETWORK_BUILD_ID := $(shell python3 ../scripts/build_identity.py --compiler=$(call shell_quote,$(CXX)) --version=$(call shell_quote,$(CXX_VERSION_LINE)) --flags=$(call shell_quote,$(WTP_NETWORK_FLAGS)) --link=$(call shell_quote,$(OPENSSL_LIBS) $(WTP_NETWORK_LDFLAGS)) --file=$(NETWORK_BUILD_RULES) $(foreach file,$(NETWORK_SYSTEM_LIBRARIES),--file=$(call shell_quote,$(file))) || printf STATE_CHECK_FAILED)
NETWORK_COMPILE_ID := $(shell python3 ../scripts/build_identity.py --compiler=$(call shell_quote,$(CXX)) --version=$(call shell_quote,$(CXX_VERSION_LINE)) --flags=$(call shell_quote,$(WTP_NETWORK_FLAGS)) --link= --file=$(NETWORK_BUILD_RULES) || printf STATE_CHECK_FAILED)
ifneq ($(filter STATE_CHECK_FAILED,$(NETWORK_BUILD_ID) $(NETWORK_COMPILE_ID)),)
$(error stage=build-identity; compiler/link identity check failed)
endif
ifeq ($(strip $(NETWORK_COMPILE_ID)),)
$(error stage=build-identity; missing compilation identity)
endif
ifeq ($(strip $(NETWORK_BUILD_ID)),)
$(error stage=build-identity; unable to identify parent network compilation inputs)
endif
endif
NETWORK_ARTIFACT_DIR := $(WTP_NETWORK_BUILD_DIR)/incremental/$(NETWORK_BUILD_ID)
NETWORK_OBJECT_DIR := $(WTP_NETWORK_BUILD_DIR)/incremental/$(NETWORK_COMPILE_ID)/obj
NETWORK_BIN_DIR := $(NETWORK_ARTIFACT_DIR)/bin
NETWORK_NAMES := wtp_tls_probe wtp_tls_contract_test wtp_plain_tcp_test wtp_configuration_test wtp_browser_api_test wtp_usb_management_test
wtp_tls_probe_SOURCES := tests/wtp_tls_probe.cpp wtp_integration/tls.cpp
wtp_tls_contract_test_SOURCES := tests/wtp_tls_contract_test.cpp wtp_integration/tls.cpp
wtp_plain_tcp_test_SOURCES := tests/wtp_plain_tcp_test.cpp wtp_integration/plain_tcp.cpp wtp_integration/tls.cpp
wtp_configuration_test_SOURCES := tests/wtp_configuration_test.cpp WTP-Client/src/codec.cpp WTP-Client/src/detail/json.cpp wtp_integration/configuration.cpp wtp_integration/consumer_radio.cpp wtp_integration/plain_tcp.cpp wtp_integration/browser_api.cpp wtp_integration/network_http.cpp wtp_integration/tls.cpp
wtp_browser_api_test_SOURCES := tests/wtp_browser_api_test.cpp wtp_integration/browser_api.cpp privileged_network_policy.cpp httplib.cpp
wtp_usb_management_test_SOURCES := tests/wtp_usb_management_test.cpp wtp_integration/usb_management.cpp wtp_integration/browser_api.cpp privileged_network_policy.cpp
NETWORK_OBJECTS := $(addprefix $(NETWORK_OBJECT_DIR)/,$(sort $(foreach name,$(NETWORK_NAMES),$($(name)_SOURCES:.cpp=.o))))
$(foreach source,$(sort $(foreach name,$(NETWORK_NAMES),$($(name)_SOURCES))),$(eval $(NETWORK_OBJECT_DIR)/$(source:.cpp=.o): $(source)))
ifneq ($(NETWORK_BUILD_ID),nonbuild)
NETWORK_INVALID_OBJECTS := $(shell python3 ../scripts/build_identity.py --invalid $(NETWORK_OBJECTS) || printf STATE_CHECK_FAILED)
ifneq ($(filter STATE_CHECK_FAILED,$(NETWORK_INVALID_OBJECTS)),)
$(error stage=dependency-validation; unable to verify object/dependency publication)
endif
-include $(wildcard $(filter-out $(NETWORK_INVALID_OBJECTS:.o=.d),$(NETWORK_OBJECTS:.o=.d)))
endif
.PHONY: FORCE_NETWORK_ADMISSION
FORCE_NETWORK_ADMISSION:
NETWORK_QUERY := $(strip $(filter --question,$(MAKEFLAGS)) $(findstring q,$(filter-out --%,$(filter -%,$(MAKEFLAGS))) $(filter-out -%,$(firstword $(MAKEFLAGS)))))
network_object_command = python3 ../scripts/build_identity.py --object $(call shell_quote,$(1)) $(call shell_quote,$(2)) --identity=$(call shell_quote,$(NETWORK_COMPILE_ID)) --compiler=$(call shell_quote,$(CXX)) --flags=$(call shell_quote,$(WTP_NETWORK_FLAGS)) $(if $(findstring -conservative,$(NETWORK_COMPILE_ID)),--conservative,)
ifeq ($(NETWORK_QUERY),)
$(NETWORK_OBJECTS): FORCE_NETWORK_ADMISSION
else
$(foreach object,$(NETWORK_OBJECTS),$(eval $(object): $(shell $(call network_object_command,$(object),$(patsubst $(NETWORK_OBJECT_DIR)/%.o,%.cpp,$(object))) --check || printf FORCE_NETWORK_ADMISSION)))
endif
$(NETWORK_OBJECT_DIR)/%.o: %.cpp
	$(Q)$(call network_object_command,$@,$<)

define NETWORK_BINARY
network_link_$(1) = python3 ../scripts/build_identity.py --binary $(call shell_quote,$(NETWORK_BIN_DIR)/$(1)) $(addprefix $(NETWORK_OBJECT_DIR)/,$($(1)_SOURCES:.cpp=.o)) --identity=$$(call shell_quote,$$(NETWORK_BUILD_ID)) --compiler=$$(call shell_quote,$$(CXX)) --flags=$$(call shell_quote,$$(WTP_NETWORK_FLAGS)) --link=$$(call shell_quote,$(if $(filter wtp_browser_api_test wtp_usb_management_test,$(1)),,$(OPENSSL_LIBS)) $(WTP_NETWORK_LDFLAGS)) $$(foreach file,$$(NETWORK_SYSTEM_LIBRARIES),--file=$$(call shell_quote,$$(file))) $$(if $$(NETWORK_CUSTOM_LINK),--conservative,)
$(NETWORK_BIN_DIR)/$(1): $(addprefix $(NETWORK_OBJECT_DIR)/,$($(1)_SOURCES:.cpp=.o)) $(NETWORK_SYSTEM_LIBRARIES) $$(if $$(NETWORK_QUERY),$$(shell $$(network_link_$(1)) --check || printf FORCE_NETWORK_ADMISSION),FORCE_NETWORK_ADMISSION)
	$$(Q)$$(network_link_$(1))
endef

# Opaque linker inputs retain unconditional linking, without preventing object reuse.
NETWORK_DEFAULT_SSL_LIBS := $(strip $(shell pkg-config --libs openssl 2>/dev/null))
NETWORK_CUSTOM_LINK := $(strip $(WTP_NETWORK_LDFLAGS) $(LIBRARY_PATH) $(findstring -conservative,$(NETWORK_COMPILE_ID)))
ifneq ($(strip $(OPENSSL_LIBS)),$(NETWORK_DEFAULT_SSL_LIBS))
NETWORK_CUSTOM_LINK := 1
endif
$(foreach name,$(NETWORK_NAMES),$(eval $(call NETWORK_BINARY,$(name))))

.PHONY: wtp-network-unit-build wtp-network-tools-build
wtp-network-unit-build: $(NETWORK_BIN_DIR)/wtp_configuration_test $(NETWORK_BIN_DIR)/wtp_browser_api_test
wtp-network-tools-build: wtp-network-unit-build $(NETWORK_BIN_DIR)/wtp_tls_probe $(NETWORK_BIN_DIR)/wtp_tls_contract_test $(NETWORK_BIN_DIR)/wtp_plain_tcp_test
