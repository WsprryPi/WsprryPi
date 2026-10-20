# MonitorFile

MonitorFile is a lightweight C++20 component for monitoring file changes by
polling filesystem metadata. Its public API remains in `src/monitorfile.hpp`,
with the implementation in `src/monitorfile.cpp`.

## Building and testing

From the WsprryPi checkout:

```sh
cd src/MonitorFile/src
make release
make test
```

The fixed standalone outputs are `build/bin/monitorfile` and
`build/bin/monitorfile_test`. The test creates its observed file under the
system temporary directory, waits for a bounded period, and removes the
temporary directory before exiting. It does not use GPIO, RF, devices,
services, installation, or elevated privileges.

To use the component in C++, include `monitorfile.hpp`, compile
`monitorfile.cpp`, and link with the platform threading support required by the
application. The class can invoke a callback after a monitored file change has
stabilized. `setPriority()` is optional and can require additional privileges
for real-time scheduling policies; the ordinary test does not call it.

## Component boundary

This directory is ordinary tracked content in the WsprryPi repository. Keep the
README, Makefile, test entry point, header, and implementation together so the
component remains independently understandable and buildable.

For standalone extraction, copy `src/MonitorFile`, initialize a new repository,
and add an appropriate license file.

## License

While part of WsprryPi, this component is covered by the repository-root
`LICENSE.md` (MIT).
