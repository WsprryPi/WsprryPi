# Raspberry Pi Mailbox Communication Component

Mailbox is a C++20 interface to the Raspberry Pi GPU mailbox and physical
memory-mapping facilities used by WsprryPi. It is retained as a coherent
component under `src/Mailbox`.

The component was written by Lee Bussy. No Broadcom source is present in this
tree.

## Retained files

```text
src/Mailbox/
├── README.md
└── src/
    ├── Makefile
    ├── bcm_model.hpp
    ├── mailbox.cpp
    ├── mailbox.hpp
    ├── mailbox_revision.cpp
    ├── mailbox_revision.hpp
    └── main.cpp
```

`mailbox.hpp`, `mailbox.cpp`, `mailbox_revision.hpp`, `mailbox_revision.cpp`, and
`bcm_model.hpp` form the reusable component.
`main.cpp` is an explicit live-device demonstration and is not an ordinary
unit test.

## Features

- C++20 mailbox interface with `[[nodiscard]]` results on critical APIs.
- Compile-time page, block, bus-flag, and peripheral-base constants.
- Big-endian parsing of `/proc/device-tree/soc/ranges` for peripheral-base
  discovery.
- Fail-closed parsing of the Raspberry Pi `Revision` field used to select
  mailbox memory-allocation flags; failed reads are not cached.
- Exceptions instead of process termination on failures.
- No dependency beyond the C++20 standard library and Linux kernel interfaces.

## Parent integration

WsprryPi compiles `src/Mailbox/src/mailbox.cpp` and includes
`src/Mailbox/src/mailbox.hpp` directly. Consumers use the public API directly:

```cpp
#include "mailbox.hpp"

extern Mailbox mailbox;
```

The live API includes `open()`, `memAlloc()`, `memLock()`, `memUnlock()`,
`memFree()`, `mapMem()`, `unMapMem()`, and `close()`.

## Standalone build and validation

From the component source directory:

```bash
cd src/Mailbox/src
make debug
make test
make -C ../.. mailbox-memory-flag-test
```

`make test` is deliberately hardware-free: it compiles the standalone demo but
does not execute it. Compilation does not qualify mailbox, memory mapping, or
Raspberry Pi behavior.

The live demonstration opens `/dev/vcio` and `/dev/mem`, allocates and maps
mailbox memory, and normally requires root privileges. It is isolated behind:

```bash
make live-test MAILBOX_LIVE_TEST=YES
```

Do not run that target without separate authorization, an identified Raspberry
Pi and attached hardware, and an explicit stopping/recovery procedure.

## Extraction

To reuse the component elsewhere, copy `src/Mailbox`, add repository metadata
and a license file, and integrate `mailbox.cpp`, `mailbox.hpp`, and
`bcm_model.hpp`. Preserve the live-device safety boundary and do not treat the
build-only test as hardware qualification.

## License

Covered by the WsprryPi root MIT license. An extracted copy should carry its own
license file.
