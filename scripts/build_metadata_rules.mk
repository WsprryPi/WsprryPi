# Shared build-metadata dependency rules.
#
# The generated C++ header is also an included makefile: its #define lines are
# comments to GNU Make.  When its content changes, GNU Make remakes the include
# and restarts before it evaluates object freshness.

BUILD_METADATA_PYTHON ?= python3
BUILD_METADATA_VERSION ?=
BUILD_METADATA_VERSION_ARGUMENT := $(if $(strip $(BUILD_METADATA_VERSION)),--release-version "$(BUILD_METADATA_VERSION)")
BUILD_METADATA_NONBUILD_GOALS := clean help macros
BUILD_METADATA_BUILD_GOALS := $(filter-out $(BUILD_METADATA_NONBUILD_GOALS),$(MAKECMDGOALS))
BUILD_METADATA_SHORT_MFLAGS := $(patsubst -%,%,$(filter-out --%,$(filter -%,$(MFLAGS))))
BUILD_METADATA_DRY_RUN := $(findstring n,$(BUILD_METADATA_SHORT_MFLAGS))
BUILD_METADATA_QUERY := $(findstring q,$(BUILD_METADATA_SHORT_MFLAGS))
BUILD_METADATA_INTROSPECTION := $(strip $(BUILD_METADATA_DRY_RUN)$(BUILD_METADATA_QUERY))

# Reject this before considering the generated include.  GNU Make otherwise
# evaluates release prerequisites before clean removes them in a mixed request.
ifneq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(strip $(filter-out clean,$(MAKECMDGOALS))),)
$(error clean is a standalone goal; run make clean and the subsequent build as separate invocations)
endif
endif

ifneq ($(strip $(BUILD_METADATA_BUILD_GOALS)),)
BUILD_METADATA_ENABLED := 1
else ifeq ($(strip $(MAKECMDGOALS)),)
BUILD_METADATA_ENABLED := 1
endif

ifeq ($(BUILD_METADATA_ENABLED),1)

ifeq ($(BUILD_METADATA_INTROSPECTION),)
# Normal builds remake this include before target freshness is decided.  GNU
# Make restarts only if the generator atomically changes the header content.
-include $(BUILD_METADATA_HEADER)

.PHONY: FORCE_BUILD_METADATA
FORCE_BUILD_METADATA:

$(BUILD_METADATA_HEADER): FORCE_BUILD_METADATA
	$(Q)$(BUILD_METADATA_PYTHON) $(BUILD_METADATA_GENERATOR) \
		--repo-root $(BUILD_METADATA_REPO_ROOT) \
		--output $@ \
		--project $(BUILD_METADATA_PROJECT) \
		--executable $(BUILD_METADATA_EXECUTABLE) $(BUILD_METADATA_VERSION_ARGUMENT)

else
# Do not let -n/-q remake included makefiles.  This read-only check models the
# header state so dry-run plans and query exit status stay accurate.
BUILD_METADATA_CHECK_STATUS := $(strip $(shell $(BUILD_METADATA_PYTHON) $(BUILD_METADATA_GENERATOR) --repo-root $(BUILD_METADATA_REPO_ROOT) --output $(BUILD_METADATA_HEADER) --project $(BUILD_METADATA_PROJECT) --executable $(BUILD_METADATA_EXECUTABLE) $(BUILD_METADATA_VERSION_ARGUMENT) --check >/dev/null 2>&1; printf '%s' $$?))

ifneq ($(filter 0 3,$(BUILD_METADATA_CHECK_STATUS)),)
else
$(error build metadata read-only check failed (status $(BUILD_METADATA_CHECK_STATUS)))
endif

.PHONY: FORCE_BUILD_METADATA
FORCE_BUILD_METADATA:

ifneq ($(BUILD_METADATA_CHECK_STATUS),0)
$(BUILD_METADATA_HEADER): FORCE_BUILD_METADATA
	$(Q)$(BUILD_METADATA_PYTHON) $(BUILD_METADATA_GENERATOR) \
		--repo-root $(BUILD_METADATA_REPO_ROOT) \
		--output $@ \
		--project $(BUILD_METADATA_PROJECT) \
		--executable $(BUILD_METADATA_EXECUTABLE) $(BUILD_METADATA_VERSION_ARGUMENT)
else
$(BUILD_METADATA_HEADER):
	$(Q)$(BUILD_METADATA_PYTHON) $(BUILD_METADATA_GENERATOR) \
		--repo-root $(BUILD_METADATA_REPO_ROOT) \
		--output $@ \
		--project $(BUILD_METADATA_PROJECT) \
		--executable $(BUILD_METADATA_EXECUTABLE) $(BUILD_METADATA_VERSION_ARGUMENT)
endif

endif

$(VERSION_RELEASE_OBJECT) $(VERSION_DEBUG_OBJECT): $(BUILD_METADATA_HEADER)

endif
