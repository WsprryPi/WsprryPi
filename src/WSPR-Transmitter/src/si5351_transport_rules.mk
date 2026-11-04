# The Linux default preserves the established i2c-dev build contract. Darwin
# must opt into the native physical transport explicitly; there is no fallback.
HOST_OS := $(shell uname -s 2>/dev/null)
ifeq ($(HOST_OS),Linux)
SI5351_TRANSPORT ?= linux-i2c
endif
# These entry points do not construct a physical device. In particular, keep
# the documented portable dispatcher usable before its simulated submake runs.
SI5351_TRANSPORT_INDEPENDENT_GOALS := help clean macros \
    semantics-test-portable semantics-make-regression-test \
    backend-capabilities-regression-test backend-capabilities-make-regression-test \
    linux-i2c-identity-test si5351-planner-test simulated-backend-test \
    simulated-backend-realtime-test transmission-controller-contract-test \
    thread-affinity-unavailable-test
SI5351_TRANSPORT_REQUIRED := $(if $(strip $(MAKECMDGOALS)),$(if $(filter-out $(SI5351_TRANSPORT_INDEPENDENT_GOALS),$(MAKECMDGOALS)),1),1)
ifneq ($(filter si5351,$(subst $(comma), ,$(BACKENDS))),)
ifeq ($(SI5351_TRANSPORT_REQUIRED),1)
ifeq ($(strip $(SI5351_TRANSPORT)),)
$(error Si5351 requires explicit SI5351_TRANSPORT selection)
endif
ifneq ($(words $(SI5351_TRANSPORT)),1)
$(error Select exactly one Si5351 transport)
endif
ifeq ($(SI5351_TRANSPORT),linux-i2c)
ifneq ($(HOST_OS),Linux)
$(error linux-i2c requires Linux)
endif
SI5351_TRANSPORT_SOURCE := si5351_transport_linux.cpp
else ifeq ($(SI5351_TRANSPORT),mcp2221a-hid)
ifneq ($(HOST_OS),Darwin)
$(error mcp2221a-hid requires macOS)
endif
SI5351_TRANSPORT_SOURCE := si5351_transport_macos.cpp
SI5351_TRANSPORT_LIBS := -framework IOKit -framework CoreFoundation
else
$(error Unknown Si5351 transport '$(SI5351_TRANSPORT)')
endif
SI5351_TRANSPORT_KEY := +transport-$(SI5351_TRANSPORT)
SI5351_TRANSPORT_FLAGS := -DWSPRRYPI_SI5351_TRANSPORT=\"$(SI5351_TRANSPORT)\"
endif
endif
