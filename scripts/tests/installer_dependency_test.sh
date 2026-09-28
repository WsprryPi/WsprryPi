#!/usr/bin/env bash
# shellcheck disable=SC1091,SC2016,SC2034,SC2329
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
INSTALLER="${SCRIPT_DIR}/../install.sh"

if grep -Eq '^[[:space:]]*22m([[:space:]]|Active High[[:space:]])*=' "${SCRIPT_DIR}/../../config/wsprrypi.ini"; then
    echo "the canonical INI must not contain retired 22m Band GPIO keys" >&2
    exit 1
fi

stock_capture_line="$(grep -nF 'stock_source_path="$source_path"' "$INSTALLER" | head -1 | cut -d: -f1)"
merge_switch_line="$(grep -nF 'source_path="$merged_ini"' "$INSTALLER" | head -1 | cut -d: -f1)"

if [[ -z "$stock_capture_line" || -z "$merge_switch_line" || "$stock_capture_line" -ge "$merge_switch_line" ]]; then
    echo "install.sh must retain the current canonical INI before selecting merged upgrade output" >&2
    exit 1
fi

if ! grep -Fq 'cp -f "${stock_source_path}" "${stock_config_path}"' "$INSTALLER"; then
    echo "install.sh must install .stock from the untouched current canonical INI" >&2
    exit 1
fi

if ! grep -Fq 'upgrade_ini "$old_path" "$source_path" "$merged_ini"' "$INSTALLER"; then
    echo "install.sh upgrades must merge old values into the current canonical INI schema" >&2
    exit 1
fi

python3 "${SCRIPT_DIR}/precompiled_binary_test.py"
"${SCRIPT_DIR}/precompiled_installer_test.sh"
python3 "${SCRIPT_DIR}/ini_upgrade_schema_test.py"
python3 "${SCRIPT_DIR}/installer_dry_run_purity_test.py"
python3 "${SCRIPT_DIR}/service_install_recovery_test.py"
"${SCRIPT_DIR}/build_resource_preflight_test.sh"

if ! awk '
    /^readonly (RUNTIME|BUILD)_APT_PACKAGES=\(/ { in_packages = 1; next }
    in_packages && /^\)/ { in_packages = 0; next }
    in_packages && /^[[:space:]]*"libssl-dev"[[:space:]]*$/ { found = 1 }
    END { exit(found ? 0 : 1) }
' "$INSTALLER"; then
    echo "libssl-dev must remain in the appropriate installer dependency group" >&2
    exit 1
fi

if ! awk '
    /^readonly BUILD_APT_PACKAGES=\(/ { in_packages = 1; next }
    in_packages && /^\)/ { in_packages = 0; next }
    in_packages && /^[[:space:]]*"libavahi-client-dev"[[:space:]]*$/ { found = 1 }
    END { exit(found ? 0 : 1) }
' "$INSTALLER"; then
    echo "libavahi-client-dev must remain in the build dependency group" >&2
    exit 1
fi
if ! awk '
    /^readonly RUNTIME_APT_PACKAGES=\(/ { in_packages = 1; next }
    in_packages && /^\)/ { in_packages = 0; next }
    in_packages && /^[[:space:]]*"libavahi-client3"[[:space:]]*$/ { found = 1 }
    END { exit(found ? 0 : 1) }
' "$INSTALLER"; then
    echo "libavahi-client3 must remain in the runtime dependency group" >&2
    exit 1
fi

for package in build-essential python3; do
    if ! awk -v required="$package" '
        /^readonly (RUNTIME|BUILD)_APT_PACKAGES=\(/ { in_packages = 1; next }
        in_packages && /^\)/ { in_packages = 0; next }
        in_packages && $0 ~ "^[[:space:]]*\"" required "\"[[:space:]]*$" { found = 1 }
        END { exit(found ? 0 : 1) }
    ' "$INSTALLER"; then
        echo "$package must remain in an installer dependency group" >&2
        exit 1
    fi
done

for package in dkms device-tree-compiler kmod; do
    if ! awk -v required="$package" '
        /^readonly RP1_GPCLK_DKMS_APT_PACKAGES=\(/ { in_packages = 1; next }
        in_packages && /^\)/ { in_packages = 0; next }
        in_packages && $0 ~ "^[[:space:]]*\"" required "\"[[:space:]]*$" { found = 1 }
        END { exit(found ? 0 : 1) }
    ' "$INSTALLER"; then
        echo "$package must remain in the conditional RP1 package list" >&2
        exit 1
    fi
done

(
    # shellcheck source=../install.sh
    source "$INSTALLER"
    warn() { :; }
    contains_package() {
        local expected="$1" package
        shift
        for package in "$@"; do
            [[ "$package" == "$expected" ]] && return 0
        done
        return 1
    }

    INSTALL_RP1_GPCLK_DKMS=false
    resolve_apt_package_list libgpiod-runtime-test
    contains_package build-essential "${RESOLVED_APT_PACKAGES[@]}"
    contains_package python3 "${RESOLVED_APT_PACKAGES[@]}"
    contains_package libgpiod-runtime-test "${RESOLVED_APT_PACKAGES[@]}"
    if contains_package dkms "${RESOLVED_APT_PACKAGES[@]}" ||
        contains_package device-tree-compiler "${RESOLVED_APT_PACKAGES[@]}" ||
        contains_package kmod "${RESOLVED_APT_PACKAGES[@]}"; then
        echo "RP1 packages leaked into an explicit opt-out" >&2
        exit 1
    fi

    INSTALL_RP1_GPCLK_DKMS=auto
    is_rp1_system() { return 1; }
    resolve_apt_package_list
    if contains_package dkms "${RESOLVED_APT_PACKAGES[@]}" ||
        contains_package "linux-headers-$(uname -r)" "${RESOLVED_APT_PACKAGES[@]}"; then
        echo "RP1 packages leaked into a non-Pi-5 automatic install" >&2
        exit 1
    fi

    is_rp1_system() { return 0; }
    resolve_apt_package_list
    contains_package dkms "${RESOLVED_APT_PACKAGES[@]}"
    contains_package device-tree-compiler "${RESOLVED_APT_PACKAGES[@]}"
    contains_package kmod "${RESOLVED_APT_PACKAGES[@]}"
    contains_package "linux-headers-$(uname -r)" "${RESOLVED_APT_PACKAGES[@]}"
)

(
    # shellcheck source=../install.sh
    source "$INSTALLER"
    dependency_fixture=$(mktemp -d /tmp/wsprrypi-rp1-dependencies.XXXXXX)
    trap 'rm -rf -- "$dependency_fixture"' EXIT
    kernel_release=6.18.34+rpt-rpi-2712
    modules_base="$dependency_fixture/lib/modules"
    headers_base="$dependency_fixture/usr/src"
    expected_headers="$headers_base/linux-headers-$kernel_release"
    fake_bin="$dependency_fixture/bin"
    mkdir -p "$modules_base/$kernel_release" "$expected_headers" "$fake_bin"
    : >"$expected_headers/Makefile"
    ln -s "$expected_headers" "$modules_base/$kernel_release/build"
    for tool in make dkms depmod modinfo sha256sum dtc fdtput cc; do
        printf '#!/bin/sh\nexit 0\n' >"$fake_bin/$tool"
        chmod 0755 "$fake_bin/$tool"
    done
    cat >"$fake_bin/dpkg-query" <<'EOF'
#!/bin/sh
if [ "${WSPRRYPi_TEST_HEADERS_MISSING:-0}" = 1 ]; then
    exit 1
fi
printf 'install ok installed\n'
EOF
    chmod 0755 "$fake_bin/dpkg-query"
    PATH="$fake_bin:/usr/bin:/bin:/usr/sbin:/sbin"
    ACTION=install
    DRY_RUN=false
    INSTALL_RP1_GPCLK_DKMS=true
    debug_start() { printf ''; }
    debug_filter() { printf ' %q' "$@"; }
    debug_print() { :; }
    debug_end() { :; }
    warn() { :; }
    command() {
        if [[ "${WSPRRYPi_TEST_FDTPUT_MISSING:-0}" == "1" &&
            "${1:-}" == "-v" && "${2:-}" == "fdtput" ]]; then
            return 1
        fi
        builtin command "$@"
    }

    validate_rp1_gpclk_build_dependencies \
        "$kernel_release" "$modules_base" "$headers_base"

    WSPRRYPi_TEST_HEADERS_MISSING=1
    export WSPRRYPi_TEST_HEADERS_MISSING
    if validate_rp1_gpclk_build_dependencies \
        "$kernel_release" "$modules_base" "$headers_base"; then
        echo "an unavailable exact header package must fail validation" >&2
        exit 1
    fi
    WSPRRYPi_TEST_HEADERS_MISSING=0
    export WSPRRYPi_TEST_HEADERS_MISSING

    WSPRRYPi_TEST_FDTPUT_MISSING=1
    if validate_rp1_gpclk_build_dependencies \
        "$kernel_release" "$modules_base" "$headers_base"; then
        echo "a missing Device Tree tool must fail validation" >&2
        exit 1
    fi
    WSPRRYPi_TEST_FDTPUT_MISSING=0

    rm "$modules_base/$kernel_release/build"
    ln -s "$headers_base/linux-headers-other" "$modules_base/$kernel_release/build"
    mkdir -p "$headers_base/linux-headers-other"
    : >"$headers_base/linux-headers-other/Makefile"
    if validate_rp1_gpclk_build_dependencies \
        "$kernel_release" "$modules_base" "$headers_base"; then
        echo "mismatched RP1 kernel headers must fail validation" >&2
        exit 1
    fi

    INSTALL_RP1_GPCLK_DKMS=false
    validate_rp1_gpclk_build_dependencies \
        missing-kernel "$dependency_fixture/missing-modules" "$dependency_fixture/missing-headers"
)

if ! awk '
    /^readonly (RUNTIME|BUILD)_APT_PACKAGES=\(/ { in_packages = 1; next }
    in_packages && /^\)/ { in_packages = 0; next }
    in_packages && /^[[:space:]]*"age"[[:space:]]*$/ { found = 1 }
    END { exit(found ? 0 : 1) }
' "$INSTALLER"; then
    echo "age must remain in the appropriate installer dependency group" >&2
    exit 1
fi

if ! awk '
    /prepare_rp1_gpclk_runtime_update "\$debug" \|\| return 1/ { recovery = NR }
    /handle_apt_packages "\$debug" \|\| return 1/ { packages = NR }
    /validate_support_bundle_age_dependency "\$debug" \|\| return 1/ { age = NR }
    /validate_rp1_gpclk_build_dependencies "\$debug" \|\| return 1/ { rp1 = NR }
    /apply_rp1_gpclk_dkms_installation "\$debug" \|\| return 1/ { apply = NR }
    END { exit(recovery > 0 && packages > recovery && age > packages && rp1 > age && apply > rp1 ? 0 : 1) }
' "$INSTALLER"; then
    echo "RP1 runtime recovery must precede package mutation and provider apply" >&2
    exit 1
fi

# shellcheck disable=SC2016
if ! grep -Fq 'validate_support_bundle_age_dependency "$debug" || return 1' "$INSTALLER"; then
    echo "install.sh must fail closed after validating age executables" >&2
    exit 1
fi

if ! awk '
    /handle_apt_packages "\$debug" \|\| return 1/ { packages = NR }
    /validate_support_bundle_age_dependency "\$debug" \|\| return 1/ { validation = NR }
    END { exit(packages > 0 && validation == packages + 1 ? 0 : 1) }
' "$INSTALLER"; then
    echo "age validation must immediately follow successful package handling" >&2
    exit 1
fi

if ! grep -Fq 'exec_command "Validate support-bundle encryption tools"' "$INSTALLER"; then
    echo "installer must route age validation through exec_command" >&2
    exit 1
fi

if grep -Fq 'Dry run: would validate the support-bundle encryption tools.' "$INSTALLER"; then
    echo "installer must not bypass the standard dry-run execution pathway for age validation" >&2
    exit 1
fi

if awk '/manage_wsprry_pi\(\)/,/^}/' "$INSTALLER" | grep -Fq 'apt-get remove age'; then
    echo "uninstall must not remove the distribution age package" >&2
    exit 1
fi

for model in \
    '["Raspberry Pi Compute Module 4S|4s-compute-module|bcm2711"]="Supported"' \
    '["Raspberry Pi Compute Module 3+|3-plus-compute-module|bcm2837"]="Supported"' \
    '["Raspberry Pi Compute Module Zero|0-compute-module|bcm2837"]="Supported"'
do
    if ! grep -Fq "$model" "$INSTALLER"; then
        echo "install.sh must recognize: $model" >&2
        exit 1
    fi
done

if grep -Eiq 'live_output|kernel_2712_phase|dtoverlay=rp1' "$INSTALLER"; then
    echo "standard installer must not deploy historical experimental RP1 paths" >&2
    exit 1
fi
if grep -Eiq 'qualification.*(tar|download|install)|dtoverlay=rp1|modprobe[[:space:]]+rp1' "$INSTALLER"; then
    echo "standard installer must not consume qualification content or activate the RP1 path" >&2
    exit 1
fi

if ! grep -Fq 'exec_command "Publish validated UI artifact"' "$INSTALLER"; then
    echo "install.sh must publish the UI through the validated artifact publisher" >&2
    exit 1
fi

if ! grep -Fq -- '--source-commit "$source_commit"' "$INSTALLER" ||
    ! grep -Fq -- '--application-version "$SEM_VER"' "$INSTALLER"; then
    echo "install.sh must bind the UI manifest to the exact source commit and application version" >&2
    exit 1
fi

for required in \
    '"--fail-on-ui-modifications 0 set_fail_on_ui_modifications' \
    '--result-file "$UI_PUBLICATION_RESULT_FILE"' \
    'publisher_args+=(--fail-on-ui-modifications)' \
    'report_ui_publication_result' \
    'trap egress EXIT'
do
    if ! grep -Fq -- "$required" "$INSTALLER"; then
        echo "install.sh is missing UI modification/reporting contract: $required" >&2
        exit 1
    fi
done

if ! awk '/egress\(\)/,/^}/' "$INSTALLER" | grep -Fq 'report_ui_publication_result'; then
    echo "the UI replacement report must be emitted from the final EXIT trap" >&2
    exit 1
fi

if awk '/manage_web\(\)/,/^}/' "$INSTALLER" | grep -Eq 'cp -r .*source_path|chown -R .*target_path'; then
    echo "manage_web must not copy or mutate the live UI tree incrementally" >&2
    exit 1
fi

echo "installer dependency tests: PASS"
