<!-- omit in toc -->
# LCBLog - A Thread-Safe C++ Logging Library

![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![C++](https://img.shields.io/badge/language-C%2B%2B17-blue)

<!-- omit in toc -->
## Table of Contents

- [📌 Overview](#-overview)
- [🚀 Features](#-features)
- [📂 Project Structure](#-project-structure-within-wsprrypi)
- [🛠️ Installation \& Compilation](#️-installation--compilation)
  - [🔧 Prerequisites](#-prerequisites)
  - [💻 Building](#-building)
- [📓 systemd / journald Integration](#-systemd--journald-integration)
  - [Log Level Mapping](#log-level-mapping)
- [📜 License](#-license)

---

## 📌 Overview

**LCBLog** is a flexible and thread-safe C++ logging library designed primarily for long-running
daemon-style applications. It provides:

- Multiple **log levels** (`DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`)
- **Thread safety** using `std::mutex`
- **Non-blocking logging** using internal worker threads and queues
- Optional **timestamps**
- **Multi-line message handling**
- Pluggable backends:
  - Standard output/error streams
  - Native **systemd journald** integration

---

## 🚀 Features

- [✔] Log level filtering
- [✔] Thread-safe, async logging
- [✔] stdout/stderr or journald backends
- [✔] Explicit journald priority mapping
- [✔] One-time startup backend banner

---

## 📂 Project Structure within WsprryPi

```text
📁 src/LCBLog
├─ README.md
└─ src
   ├─ lcblog.hpp
   ├─ lcblog.cpp
   ├─ lcblog.tpp
   ├─ main.cpp
   └─ Makefile
```

---

## 🛠️ Installation & Compilation

### 🔧 Prerequisites

- C++20
- make
- Optional: libsystemd-dev

### 💻 Building

```bash
cd src/LCBLog/src
make
```

To extract LCBLog for reuse, copy `src/LCBLog` into a new repository, add the
desired repository metadata and license file, and run the same build from its
`src` directory. The component does not depend on WsprryPi headers,
configuration, globals, runtime services, hardware, or directory layout.

---

## 📓 systemd / journald Integration

Install systemd headers:

```bash
sudo apt install libsystemd-dev
```

Enable journald:

```cpp
logger.enableJournald(true);
logger.setJournaldIdentifier("wsprrypi");
```

### Log Level Mapping

| LCBLog | journald |
|------|----------|
| DEBUG | 7 |
| INFO  | 6 |
| WARN  | 4 |
| ERROR | 3 |
| FATAL | 2 |

---

## 📜 License

Covered by the WsprryPi root MIT license. An extracted copy should carry its own
license file.

## Shutdown validation

Logger destruction publishes its shutdown predicate under both queue mutexes, wakes the workers and joins them after releasing the locks. Pending messages drain before destruction returns. Logging producers must stop before the logger is destroyed.

From the WsprryPi repository root on Linux with the existing C/C++ toolchain and libsystemd development package, run `python3 scripts/tests/logger_shutdown_test.py`. This compiles the actual component, forces the predicate-to-wait scheduling window and checks pending-record drain. `python3 scripts/tests/early_cli_exit_test.py src/build/bin/wsprrypi` checks bounded informational CLI exits against an already built application; neither command starts the application service or submits RF jobs.
