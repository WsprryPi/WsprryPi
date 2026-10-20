# Singleton

Singleton is a header-only C++20 component that enforces one application
instance by holding a UDP socket bound to a chosen loopback port. The reusable
API is entirely in `src/singleton.hpp`.

## Building and safe testing

From the WsprryPi checkout:

```sh
cd src/Singleton/src
make test
```

The fixed test output is `singleton_test`. The test asks the operating system
for an available loopback UDP port, verifies that the first instance acquires
it, verifies that a simultaneous second instance is rejected, and confirms that
the port can be acquired again after the first instance is destroyed. It uses
no fixed operator port, privileged port, external process, service, hardware,
or elevated permission.

The standalone demonstration includes fixed-port, child-process, and
restricted-port examples. It is excluded from
ordinary validation and guarded behind explicit opt-in:

```sh
make demo SINGLETON_DEMO=YES
```

## Usage

```cpp
#include "singleton.hpp"

SingletonProcess singleton(8080);
if (!singleton()) {
    // Another process already owns this application's selected lock port.
}
```

Choose a stable, application-specific loopback port for production. The socket
is released when `SingletonProcess` is destroyed.

## Component and extraction boundary

This directory is ordinary tracked content in the WsprryPi repository. Keep the
header, README, Makefile, bounded test, and demonstration together.

For standalone extraction, copy `src/Singleton`, initialize a new repository,
and add an appropriate license file.

## License

While part of WsprryPi, this component is covered by the repository-root
`LICENSE.md` (MIT).
