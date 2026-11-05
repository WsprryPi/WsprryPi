# These fixtures have the same compilation flags and include search order.
# Configuration-client, standalone WTP archives and USB-pair objects stay separate.
shell_quote = '$(subst ','"'"',$(1))'
NETWORK_BUILD_RULES := ../scripts/network_build_rules.mk
NETWORK_SYSTEM_LIBRARIES := $(wildcard $(shell pkg-config --variable=libdir openssl 2>/dev/null)/libssl.* $(shell pkg-config --variable=libdir openssl 2>/dev/null)/libcrypto.*)
ifeq ($(strip $(filter-out clean help macros,$(MAKECMDGOALS))),)
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
wtp_browser_api_test_SOURCES := tests/wtp_browser_api_test.cpp wtp_integration/browser_api.cpp privileged_network_policy.cpp
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
.PHONY: FORCE_NETWORK_DEPENDENCIES
FORCE_NETWORK_DEPENDENCIES:
$(NETWORK_INVALID_OBJECTS): FORCE_NETWORK_DEPENDENCIES
ifneq ($(findstring -conservative,$(NETWORK_COMPILE_ID)),)
$(NETWORK_OBJECTS): FORCE_NETWORK_DEPENDENCIES
endif

$(NETWORK_OBJECT_DIR)/%.o: %.cpp
	$(Q)mkdir -p "$(@D)"
	$(Q)set -e; trap 'rm -f "$@.tmp" "$(@:.o=.d).tmp" "$(@:.o=.state).tmp"' 0; \
		$(CXX) $(WTP_NETWORK_FLAGS) -MD -MP -MF "$(@:.o=.d).tmp" -MT "$@" -c "$<" -o "$@.tmp"; \
		python3 ../scripts/build_identity.py --record "$@.tmp" "$(@:.o=.d).tmp" "$(@:.o=.state).tmp"; \
		mv "$(@:.o=.d).tmp" "$(@:.o=.d)"; mv "$(@:.o=.state).tmp" "$(@:.o=.state)"; mv "$@.tmp" "$@"

define NETWORK_BINARY
$(NETWORK_BIN_DIR)/$(1): $(addprefix $(NETWORK_OBJECT_DIR)/,$($(1)_SOURCES:.cpp=.o)) $(NETWORK_SYSTEM_LIBRARIES)
	$$(Q)mkdir -p "$$(@D)"
	$$(Q)set -e; trap 'rm -f "$$@.tmp"' 0; \
		$$(CXX) $$(WTP_NETWORK_FLAGS) $$(filter %.o,$$^) $(if $(filter wtp_browser_api_test wtp_usb_management_test,$(1)),,$(OPENSSL_LIBS)) $(WTP_NETWORK_LDFLAGS) -o "$$@.tmp"; \
		mv "$$@.tmp" "$$@"
endef
$(foreach name,$(NETWORK_NAMES),$(eval $(call NETWORK_BINARY,$(name))))

# Arbitrary caller linker syntax may hide inputs (-l, response files, scripts).
# Preserve its former relink-every-time behavior; only default known SSL linkage
# receives link reuse. Compilation remains incremental in either case.
NETWORK_DEFAULT_SSL_LIBS := $(strip $(shell pkg-config --libs openssl 2>/dev/null))
NETWORK_CUSTOM_LINK := $(strip $(WTP_NETWORK_LDFLAGS) $(LIBRARY_PATH) $(findstring -conservative,$(NETWORK_COMPILE_ID)))
ifneq ($(strip $(OPENSSL_LIBS)),$(NETWORK_DEFAULT_SSL_LIBS))
NETWORK_CUSTOM_LINK := 1
endif
ifneq ($(NETWORK_CUSTOM_LINK),)
.PHONY: FORCE_NETWORK_LINK
FORCE_NETWORK_LINK:
$(addprefix $(NETWORK_BIN_DIR)/,$(NETWORK_NAMES)): FORCE_NETWORK_LINK
endif

.PHONY: wtp-network-unit-build wtp-network-tools-build
wtp-network-unit-build: $(NETWORK_BIN_DIR)/wtp_configuration_test $(NETWORK_BIN_DIR)/wtp_browser_api_test
wtp-network-tools-build: wtp-network-unit-build $(NETWORK_BIN_DIR)/wtp_tls_probe $(NETWORK_BIN_DIR)/wtp_tls_contract_test $(NETWORK_BIN_DIR)/wtp_plain_tcp_test
