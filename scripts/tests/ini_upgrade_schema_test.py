#!/usr/bin/env python3
"""Exercise the installer's INI migration against the current schema."""

from __future__ import annotations

import shutil
import configparser
import re
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "scripts" / "install.sh"
CURRENT_INI = ROOT / "config" / "wsprrypi.ini"


def extract_upgrade_program(installer: str) -> str:
    function_start = installer.index("upgrade_ini() {")
    program_start = installer.index("    if awk '\n", function_start) + len("    if awk '\n")
    program_end = installer.index("\n    ' \"$old_ini\" \"$new_ini\"", program_start)
    return installer[program_start:program_end]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def parse_ini(text: str) -> configparser.ConfigParser:
    data = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";", "#"))
    data.optionxform = str
    data.read_string(text)
    return data


def check_persistent_schema(current: str) -> None:
    """Use the runtime persistence allowlist, including its dynamic key sets."""
    source = (ROOT / "src/config_handler.cpp").read_text(encoding="utf-8")
    allowlist = source.split("const bool persist_key =", 1)[1].split("if (!persist_key)", 1)[0]
    sections = list(re.finditer(r'section_name == "([^"]+)"', allowlist))
    required = {}
    for index, section in enumerate(sections):
        end = sections[index + 1].start() if index + 1 < len(sections) else len(allowlist)
        required[section[1]] = set(re.findall(r'key == "([^"]+)"', allowlist[section.end():end]))
    for section, header, function in (
        ("WTP", "wtp_settings_json.hpp", "wtp_settings_json"),
        ("WTP Server", "wtp_server_settings_json.hpp", "wtp_server_settings_json"),
    ):
        serializer = (ROOT / "src" / header).read_text(encoding="utf-8").split(function, 1)[1].split("inline ", 1)[0]
        required[section] = set(re.findall(r'\{"([^"]+)"\s*,', serializer))
        require(bool(required[section]), f"no runtime keys extracted for {section}")
    bands = source.split("kHamBandJsonKeys =", 1)[1].split("}};", 1)[0]
    required["Band GPIO"] = {key for band in re.findall(r'"([^"]+)"', bands)
                            for key in (band, f"{band} Active High")}
    stock = parse_ini(current)
    missing = [(section, key) for section, keys in required.items() for key in sorted(keys)
               if not stock.has_option(section, key)]
    require(not missing, f"stock INI omits runtime-persisted settings: {missing}")


def upgrade(
    awk: str,
    program: str,
    old_ini: Path,
    current_ini: Path,
    output: Path,
) -> str:
    with output.open("w", encoding="utf-8") as stream:
        subprocess.run(
            [awk, program, str(old_ini), str(current_ini)],
            check=True,
            stdout=stream,
            text=True,
        )
    return output.read_text(encoding="utf-8")


def main() -> None:
    awk = shutil.which("awk")
    require(awk is not None, "no awk implementation is available")

    current = CURRENT_INI.read_text(encoding="utf-8")
    check_persistent_schema(current)
    require("\n22m =" not in current, "current INI still contains 22m")
    require(
        "\n22m Active High =" not in current,
        "current INI still contains 22m Active High",
    )
    require(
        current.count("RP1 Drive mA = 2") == 1,
        "current INI must declare the canonical RP1 drive default exactly once",
    )

    program = extract_upgrade_program(INSTALLER.read_text(encoding="utf-8"))

    with tempfile.TemporaryDirectory(prefix="wsprrypi-ini-upgrade-") as directory:
        root = Path(directory)
        old_ini = root / "old.ini"
        merged_ini = root / "merged.ini"
        stock_ini = root / "installed.ini.stock"

        custom = {
            "WTP": {"Transport": "network", "Hostname": "station.example", "TCP Port": "31418",
                    "TLS Server Identity": "station.example", "TLS CA File": "/etc/station/ca.pem",
                    "TLS Client Certificate": "/etc/station/client.pem", "TLS Client Key": "/etc/station/client.key",
                    "Endpoint": "/dev/serial/by-id/station", "USB Serial": "001234",
                    "USB Vendor ID": "11914", "USB Product ID": "10", "Device ID": "0123456789abcdef0123456789abcdef",
                    "Allow Frequency Adjustment": "true", "Start Uncertainty ns": "1000000"},
            "WSPR": {"Frequency Profile": "wrc15", "Band Preferences": '{"20m":14097100}'},
            "Band GPIO": {"8m": "23", "8m Active High": "true", "5m": "24", "5m Active High": "true"},
        }
        old_ini.write_text("".join(f"[{section}]\n" + "".join(f"{key} = {value}\n" for key, value in keys.items())
                                   for section, keys in custom.items()) + "\n[Unknown]\nSetting = ignored\n", encoding="utf-8")
        original = old_ini.read_bytes()
        # Run the actual shell publication/backup path, not just the AWK program.
        result = subprocess.run(["bash", "-c", r'''
source "$1"
DRY_RUN=false
debug_start() { :; }; debug_filter() { printf '%q ' "$@"; }; debug_print() { :; }
debug_end() { :; }; logE() { :; }
exec_command() {
    shift
    local args=() arg
    for arg in "$@"; do [[ -z "$arg" ]] || args+=("$arg"); done
    "${args[@]}"
}
upgrade_ini "$2" "$3" "$4"
''', "ini-preservation", str(INSTALLER), str(old_ini), str(CURRENT_INI), str(merged_ini)],
                                capture_output=True, text=True, check=False)
        require(result.returncode == 0, f"custom merge failed: {result.stderr}")
        merged = merged_ini.read_text(encoding="utf-8")
        parsed = parse_ini(merged)
        for section, keys in custom.items():
            for key, value in keys.items():
                require(parsed.get(section, key) == value, f"custom {section}.{key} was lost")
        require(not parsed.has_section("Unknown"), "unknown settings passed through")
        require(old_ini.read_bytes() == original, "custom merge changed installed input")
        require(Path(f"{old_ini}.pre_migration.bak").read_bytes() == original, "custom merge changed backup")
        require(upgrade(awk, program, merged_ini, CURRENT_INI, root / "custom-repeat.ini") == merged,
                "custom repeat installation is not idempotent")

        for token, enabled in (("true", True), ("T", True), ("1", True), ("yes", True),
                               ("Y", True), ("on", True), ("false", False), ("F", False),
                               ("0", False), ("no", False), ("N", False), ("off", False)):
            for canonical in (None, "true", "false"):
                for manual in (None, "-7.25"):
                    old_ini.write_text(f"[Calibration]\nPPM = 3.125\n[GPIO]\nUse NTP = {token}\n" +
                                       (f"Use System Clock Frequency Estimate = {canonical}\n" if canonical else "") +
                                       (f"Manual PPM = {manual}\n" if manual else ""), encoding="utf-8")
                    migrated = parse_ini(upgrade(awk, program, old_ini, CURRENT_INI, merged_ini))
                    require(migrated.getboolean("GPIO", "Use System Clock Frequency Estimate") ==
                            (canonical == "true" if canonical else enabled), "legacy clock policy changed")
                    expected_manual = manual if manual else ("0.0" if enabled else "3.125")
                    require(migrated.get("GPIO", "Manual PPM") == expected_manual, "legacy manual PPM changed")
                    require(migrated.get("GPIO", "Frequency Residual PPM") == "0.0", "legacy residual missing")
                    require(not migrated.has_option("GPIO", "Use NTP"), "retired clock key persisted")
        for calibration in ("", "PPM =\n", "PPM = -2.5\n"):
            old_ini.write_text("[Calibration]\n" + calibration +
                               "[GPIO]\nUse NTP = false\nFrequency Residual PPM = 1.75\n", encoding="utf-8")
            migrated = parse_ini(upgrade(awk, program, old_ini, CURRENT_INI, merged_ini))
            require(migrated.get("GPIO", "Manual PPM") == ("-2.5" if "-2.5" in calibration else "0.0"),
                    "missing/blank legacy calibration was not handled")
            require(migrated.get("GPIO", "Frequency Residual PPM") == "1.75", "custom residual was lost")
        for invalid in ("Use NTP = maybe", "Use NTP = false\nUse System Clock Frequency Estimate = invalid"):
            old_ini.write_text(f"[GPIO]\n{invalid}\n", encoding="utf-8")
            result = subprocess.run([awk, program, str(old_ini), str(CURRENT_INI)], capture_output=True, text=True)
            require(result.returncode != 0, "invalid legacy boolean was silently normalized")
        old_ini.write_bytes(b"[Calibration]\r\nPPM = 3.125\r\n[GPIO]\r\nUse NTP = false\r\n")
        migrated = parse_ini(upgrade(awk, program, old_ini, CURRENT_INI, merged_ini))
        require(not migrated.getboolean("GPIO", "Use System Clock Frequency Estimate"), "CRLF policy lost")
        require(migrated.get("GPIO", "Manual PPM") == "3.125", "CRLF calibration lost")

        # Exercise the actual shell function, including failure handling. A
        # failed or empty merge must never publish defaults over custom gates.
        for failure in ("nonzero", "empty", "awk-only"):
            old_ini.write_text("[WTP Server]\nEnabled = false\n", encoding="utf-8")
            merged_ini.write_text("previous merged output\n", encoding="utf-8")
            before = old_ini.read_bytes(), merged_ini.read_bytes(), CURRENT_INI.read_bytes()
            result = subprocess.run(
                ["bash", "-c", r'''
source "$1"
old_path=$2; new_path=$3; merged_path=$4; merge_failure_case=$5
DRY_RUN=false
debug_start() { :; }; debug_filter() { printf '%q ' "$@"; }; debug_print() { :; }
debug_end() { :; }; logE() { :; }
exec_command() {
    shift
    local args=() arg
    for arg in "$@"; do [[ -z "$arg" ]] || args+=("$arg"); done
    "${args[@]}"
}
awk() {
    [[ "$merge_failure_case" != awk-only ]] || { command awk "$@"; return; }
    [[ "$merge_failure_case" == empty ]] && return 0
    return 23
}
mawk() { return 127; }
if [[ "$merge_failure_case" == awk-only ]]; then
    upgrade_ini "$old_path" "$new_path" "$merged_path"
elif upgrade_ini "$old_path" "$new_path" "$merged_path"; then
    exit 90
fi
''', "ini-merge-fixture", str(INSTALLER), str(old_ini), str(CURRENT_INI),
                 str(merged_ini), failure],
                capture_output=True, text=True, check=False,
            )
            require(result.returncode == 0, f"{failure} merge did not fail closed: {result.stderr}")
            require((before[0], before[2]) == (old_ini.read_bytes(), CURRENT_INI.read_bytes()),
                    f"{failure} merge replaced its inputs")
            if failure == "awk-only":
                require("Enabled = false" in merged_ini.read_text(encoding="utf-8"),
                        "awk-only merge lost the disabled WTP listener")
            else:
                require(before[1] == merged_ini.read_bytes(), f"{failure} merge replaced configuration")
            require(Path(f"{old_ini}.pre_migration.bak").read_bytes() == before[0],
                    f"{failure} merge lost the recovery copy")
            require(not Path(f"{merged_ini}.tmp").exists(), f"{failure} merge retained partial output")

        old_ini.write_text(
            "[Band GPIO]\n"
            "22m = 17\n"
            "22m Active High = true\n"
            "20m = 23\n"
            "20m Active High = true\n"
            "\n"
            "[WSPR]\n"
            "Call Sign = UPGRADE\n",
            encoding="utf-8",
        )

        merged = upgrade(awk, program, old_ini, CURRENT_INI, merged_ini)

        shutil.copyfile(CURRENT_INI, stock_ini)

        require("22m =" not in merged, "upgrade retained retired 22m key")
        require(
            "22m Active High =" not in merged,
            "upgrade retained retired 22m Active High key",
        )
        require("20m = 23" in merged, "upgrade did not preserve current-schema value")
        require(
            "20m Active High = true" in merged,
            "upgrade did not preserve current-schema polarity",
        )
        require("Call Sign = UPGRADE" in merged, "upgrade did not preserve operator value")
        require(
            merged.count("RP1 Drive mA = 2") == 1,
            "legacy upgrade did not add exactly one canonical RP1 drive default",
        )
        require(
            stock_ini.read_bytes() == CURRENT_INI.read_bytes(),
            "installed stock basis differs from the current canonical INI",
        )
        require(
            stock_ini.read_bytes() != merged_ini.read_bytes(),
            "installed stock incorrectly uses merged active configuration",
        )

        repeated_ini = root / "repeated.ini"
        repeated = upgrade(awk, program, merged_ini, CURRENT_INI, repeated_ini)
        require(repeated == merged, "repeat upgrade changed the merged configuration")

        duplicate_old_ini = root / "duplicate-old.ini"
        duplicate_merged_ini = root / "duplicate-merged.ini"
        duplicate_old_ini.write_text(
            "[GPIO]\n"
            "Transmit Pin = 4\n"
            "\n"
            "[Band GPIO]\n"
            "20m = 23\n"
            "\n"
            "[GPIO]\n"
            "RP1 Drive mA = 8\n",
            encoding="utf-8",
        )
        duplicate_merged = upgrade(
            awk,
            program,
            duplicate_old_ini,
            CURRENT_INI,
            duplicate_merged_ini,
        )
        require(
            duplicate_merged.count("[GPIO]") == 1,
            "upgrade retained a duplicate legacy GPIO section",
        )
        require(
            duplicate_merged.count("RP1 Drive mA = 8") == 1,
            "upgrade did not canonicalize the retained RP1 drive value",
        )

        for name, old_value, expected in (
            ("valid", "8", "RP1 Drive mA = 8"),
            ("invalid", "6", "RP1 Drive mA = 6"),
            ("blank", "", "RP1 Drive mA = "),
        ):
            value_old_ini = root / f"{name}-old.ini"
            value_merged_ini = root / f"{name}-merged.ini"
            value_old_ini.write_text(
                "[GPIO]\n" f"RP1 Drive mA = {old_value}\n",
                encoding="utf-8",
            )
            value_merged = upgrade(
                awk,
                program,
                value_old_ini,
                CURRENT_INI,
                value_merged_ini,
            )
            require(
                expected in value_merged,
                f"upgrade did not preserve {name} RP1 drive value",
            )
            require(
                value_merged.count("RP1 Drive mA =") == 1,
                f"upgrade emitted duplicate RP1 drive keys for {name} value",
            )

    print("INI upgrade schema lifecycle test: PASS")


if __name__ == "__main__":
    main()
