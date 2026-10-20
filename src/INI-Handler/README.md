# INI File Handler

A lightweight, production-ready C++20 INI parser designed for WsprryPi.

This parser preserves formatting and comments while providing safe, explicit,
and atomic configuration management.

---

## Overview

The INI File Handler is designed around a **dual-file model**:

- **Live configuration** (user-modified)
- **Stock configuration** (canonical schema)

It separates:

- **Structure** → defined by the stock file
- **State** → stored in the live file

This ensures deterministic behavior, safe updates, and reliable recovery.

---

## Features

- Meyers' singleton (`iniFile`)
- Preserves comments, ordering, and formatting
- Strongly typed getters:
  - string
  - bool
  - int
  - double
- Exception-based error handling
- Explicit commit model
- Atomic file writes
- Stock-based repair and reset
- Minimal dependencies (C++20)

---

## File Layout (WsprryPi)

```text
/usr/local/etc/wsprrypi.ini        # live config
/usr/local/etc/wsprrypi.ini.stock  # canonical template
```

---

## API Quick Reference

| Function | Description |
|----------|-------------|
| `set_filename(path)` | Set active INI file |
| `get_string_value(section, key)` | Get string value |
| `get_bool_value(section, key)` | Get boolean value |
| `get_int_value(section, key)` | Get integer value |
| `get_double_value(section, key)` | Get double value |
| `set_string_value(section, key, value)` | Set string value |
| `set_int_value(section, key, value)` | Set integer value |
| `commit_changes()` | Persist pending changes |
| `save()` | Write changes (atomic) |
| `reset_to_stock()` | Replace live config with stock |
| `repair_from_stock()` | Rebuild from stock and merge valid values |

---

## Behavior

### Getters

Throw `std::runtime_error` if:

- Section is missing
- Key is missing
- Type conversion fails

---

### commit_changes()

- Writes only modified values
- Uses `save()` internally
- Preserves file structure and comments
- Persists explicitly added sections and keys

---

### save() (Atomic)

Writes safely using:

```text
write → .tmp
rename → live file
```

Prevents partial writes or corruption.

---

### repair_from_stock()

- Rebuilds config using stock structure
- Merges valid user values
- Drops unknown or invalid entries
- Writes atomically

---

### reset_to_stock()

- Fully replaces live configuration
- Applies semantic version token replacement
- Discards all user changes

---

## Usage

### Loading

```cpp
iniFile.set_filename("/usr/local/etc/wsprrypi.ini");

auto call = iniFile.get_string_value("Common", "Call Sign");
```

---

### Writing

```cpp
iniFile.set_string_value("Common", "Call Sign", "AA0NT");
iniFile.commit_changes();
```

---

## Error Handling

All operations throw `std::runtime_error`.

```cpp
try {
    auto val = iniFile.get_int_value("Common", "TX Power");
} catch (const std::runtime_error &e) {
    // Handle error
}
```

---

## Design Philosophy

- Preserve user edits
- Never silently overwrite
- Keep parsing deterministic
- Separate schema from state
- Provide explicit recovery paths

---

## Limitations

- Does not auto-create missing keys during normal operation
- No semantic validation beyond type safety
- Schema changes require repair/reset

---

## Installer Behavior

- Always install/update stock file
- Only create live config if missing

```bash
if [[ ! -f /usr/local/etc/wsprrypi.ini ]]; then
    cp wsprrypi.ini.stock wsprrypi.ini
fi
```

---

## License

Covered by the WsprryPi root MIT license.

---

## Author

Lee C. Bussy (@LBussy)
