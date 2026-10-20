# WSPR Reference Implementation

A correctness-first WSPR reference encoder/decoder with explicit ambiguity handling.

This component is ordinary tracked content in the WsprryPi repository. Debian
build, regression, install/export, and consumer coverage runs in the parent
repository's non-hardware workflow.

## Quick Start

```bash
cmake -S . -B build
cmake --build build

./build/wspr-encode AA0NT EM18 30
./build/wspr-decode --quiet 330020021022111022120121133220000232032322002012130033030021303020033032301010232030110201323012223220221021021112330211212021312202030320112222222330323322013222
./build/wspr-correlate --quiet 330220021020113022100321133020200230032120022210130233030001301022033232321210012232130003103030203220001221023112330233230223312202030122132020202132103322031020 332002201020111022320121311022222212032302022230130033230021301202031232301032230032310221303032221222201023223312130031030203132200010100132000220330303120213202
```

## Overview

This project is a clean, testable WSPR reference implementation focused on
correctness, transparency, and reproducibility.

It supports:

- Type 1 messages
- Type 2 messages, including prefix and suffix forms
- Type 3 messages with hashed callsign and 6-character locator
- Type 2 and Type 3 correlation
- Human-readable, quiet, and JSON CLI output modes
- Regression coverage for known ambiguity in the Type 2 overlap region
- A reusable, installable C++ library API

## Components

- `wspr-encode`
- `wspr-decode`
- `wspr-correlate`
- `wspr_ref_lib`
- Regression suite
- API example

## CLI Tools

### Encode

```bash
./build/wspr-encode AA0NT EM18 30
./build/wspr-encode --quiet AA0NT EM18 30
./build/wspr-encode --symbols-only AA0NT EM18 30
./build/wspr-encode --json AA0NT EM18 30
```

Default output:

```text
Type: TYPE1
Callsign: AA0NT
Locator: EM18
Power: 30 dBm
Symbols: 330020021022111022120121133220000232032322002012130033030021303020033032301010232030110201323012223220221021021112330211212021312202030320112222222330323322013222
```

JSON output:

```json
{
  "type": "TYPE1",
  "callsign": "AA0NT",
  "locator": "EM18",
  "power_dbm": 30,
  "symbols": "330020021022111022120121133220000232032322002012130033030021303020033032301010232030110201323012223220221021021112330211212021312202030320112222222330323322013222"
}
```

### Decode

```bash
./build/wspr-decode <symbols>
./build/wspr-decode --quiet <symbols>
./build/wspr-decode --json <symbols>
```

Quiet output examples:

```text
TYPE1 AA0NT EM18 30
TYPE2 <hashed> /09 30 ALT /Z
TYPE3 <hashed> 6521 EM18IG 30
```

JSON fields:

```json
{
  "type": "TYPE2",
  "callsign": "<hashed>",
  "power_dbm": 30,
  "is_partial": true,
  "has_hash": false,
  "has_ambiguity": true,
  "extra": "/09",
  "alternate_extra": "/Z"
}
```

When no message type unpacks successfully, `--json` emits:

```json
{
  "type": "UNKNOWN",
  "error": "No message type unpack succeeded."
}
```

### Correlate

```bash
./build/wspr-correlate <symbols1> <symbols2>
./build/wspr-correlate --quiet <type2> <type3>
./build/wspr-correlate --json <type2> <type3>
```

Quiet output examples:

```text
CORRELATED TYPE2 <hashed>/12 6521 EM18IG 30
CORRELATED TYPE2 <hashed>/09 6521 EM18IG 30 ALT /Z
UNCORRELATED1 TYPE2 <hashed> /12 30
UNCORRELATED2 TYPE3 <hashed> 6521 EM18IG 30
```

Correlated JSON output:

```json
{
  "type": "TYPE2",
  "callsign": "<hashed>/12",
  "power_dbm": 30,
  "is_partial": true,
  "has_hash": true,
  "has_ambiguity": false,
  "locator": "EM18IG",
  "hash": 6521,
  "status": "CORRELATED"
}
```

Uncorrelated JSON output contains:

```json
{
  "status": "UNCORRELATED",
  "message1": {
    "type": "TYPE2"
  },
  "message2": {
    "type": "TYPE3"
  }
}
```

## Type 2 Ambiguity Policy

Overlap region:

```text
/Z  ⇄  /09
/Q  ⇄  /00
```

Policy:

- Deterministic primary decode
- Alternate preserved
- No silent loss

## Regression Testing

```bash
./tests/run_major_regressions.sh
```

Overlap cases are observational, not failures.

## Reusable Library

```cpp
auto encoded = wspr::encode_message("AA0NT", "EM18", 30);
```

## Install

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix ./build/install
```

This installs the library, all public-facing headers under `include/wspr/`, and
the exported CMake package files under `lib/cmake/wspr_ref/`.

## Using (CMake)

```cmake
find_package(wspr_ref CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE wspr::wspr_ref_lib)
```

If installed under a custom prefix, point CMake at it:

```bash
cmake -S examples/consumer -B build-consumer -DCMAKE_PREFIX_PATH="$PWD/build/install"
cmake --build build-consumer
./build-consumer/consumer
```

Minimal consumer:

```cpp
#include "wspr/wspr_ref_api.hpp"

#include <iostream>
#include <string>

int main()
{
    const auto encoded = wspr::encode_message("AA0NT", "EM18", 30);
    if (!encoded.ok)
        return 1;

    wspr::WsprDecodedMessage decoded;
    std::string error;
    if (!wspr::decode_symbols(encoded.symbols, decoded, error))
        return 1;

    std::cout << decoded.callsign << "\n";
    return 0;
}
```

## License

WsprryPi-authored code in this component is covered by the repository-root
`LICENSE.md` (MIT). The bundled `include/nlohmann/json.hpp` retains its embedded
third-party copyright and SPDX notices; the corresponding MIT, CC0-1.0, and
Apache-2.0 texts are in `docs/licenses/nlohmann-json/`.

## Extraction guidance

To extract WSPR-Reference, copy `src/WSPR-Reference`, initialize a new
repository, add an appropriate license for the WSPR-Reference code, and retain
the bundled-header notices plus the license texts identified above.
