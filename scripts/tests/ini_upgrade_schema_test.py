#!/usr/bin/env python3
"""Exercise the installer's INI migration against the current schema."""

from __future__ import annotations

import shutil
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
