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

## Native installation ownership

`src/installation_lock.hpp` adds `InstallationLock(directory)` for an installation with multiple independent state owners. Supply the fixed private owner directory, retain the guard for the entire operation, and call `operator()()` to acquire it. A simultaneous owner returns `false`; unsafe storage or an operating-system failure throws. The class is noncopyable and nonmovable.

Linux and macOS use a nonblocking exclusive `flock`; Windows uses a nonblocking exclusive `LockFileEx` byte-range lock. The zero-byte `instance.lock` file remains after release. Never delete it to clear contention: deleting a live lock file can create two owners on different inodes. Operating-system process cleanup releases ownership after abrupt termination. Existing database locks remain necessary for database integrity. The POSIX overload taking an expected owner UID lets root maintenance acquire an existing backend-owned lock; it cannot create that owner’s directory or file.

The directory is created privately under an existing trusted owner parent. POSIX paths reject symbolic links, untrusted writable ancestors, wrong owners, permissive access modes, hard links, and unexpected file contents. macOS also checks extended ACL grants. Windows creates a protected owner/System/Administrators DACL, checks existing owners and ACLs, rejects reparse points and hard links, and retains directory handles without delete sharing. Only local fixed-drive paths are accepted on Windows. Network filesystems are not qualified by these tests.

The UDP API remains available; Windows uses Winsock with exclusive address ownership. Controller installations use the file-lock API, while ordinary Pi operation retains its UDP guard.

The portable standalone build is:

```sh
cmake -S src/Singleton -B /tmp/wsprrypi-singleton-build
cmake --build /tmp/wsprrypi-singleton-build
ctest --test-dir /tmp/wsprrypi-singleton-build --output-on-failure
```

On Windows, select a private build directory using native path syntax. The file-lock tests use private temporary state, verify competing processes and reacquisition, and reject unsafe storage. POSIX tests additionally terminate their own child to check release after abrupt death. Windows test sources require a native Windows runner for runtime qualification; cross-compilation only checks the build.
