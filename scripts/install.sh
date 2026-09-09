#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

# -----------------------------------------------------------------------------
# @file install.sh
# @brief Trap unexpected errors during script execution.
# @details Captures any errors (via the ERR signal) that occur during script
#          execution. Logs the function name and line number where the error
#          occurred and exits the script. The trap calls an error-handling
#          function for better flexibility.
#
# @author Lee C. Bussy <Lee@Bussy.org>
#
# @license MIT License
#
# Copyright © 2023 - 2026 Lee C. Bussy
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# @usage
# sudo ./install.sh
# sudo ./install.sh debug
# sudo DRY_RUN=true ./install.sh debug
# sudo ACTION=uninstall ./install.sh
# REPO_BRANCH=dev ./install.sh --debug
# curl -fsSL {url} | sudo bash
# curl -fsSL {url} | sudo bash -s -- debug
# curl -fsSL {url} | sudo env DRY_RUN=true bash -s -- debug
# curl -fsSL {url} | sudo env ACTION=uninstall bash -s -- debug
#
# -----------------------------------------------------------------------------

declare DEFAULT_REPO_BRANCH="main"
declare DEFAULT_SEM_VER="3.2.0"

# -----------------------------------------------------------------------------
# @brief Trap unexpected errors during script execution.
# @details Captures any errors (via the ERR signal) that occur during script
#          execution.
# Logs the function name and line number where the error occurred and exits
# the script. The trap calls an error-handling function for better flexibility.
#
# @global FUNCNAME Array containing function names in the call stack.
# @global LINENO Line number where the error occurred.
# @global THIS_SCRIPT Name of the script.
#
# @return None (exits the script with an error code).
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
trap_error() {
    # Capture function name, line number, and script name
    local func="${FUNCNAME[1]:-main}" # Get the call function (default: "main")
    local line="${1:-}"               # Line number where the error occurred
    local script="${this_script:-$(basename "${BASH_SOURCE[0]:-$0}")}"

    # Use global THIS_SCRIPT if it exists and is not empty;
    # otherwise, determine the script name
    if [[ -n "${THIS_SCRIPT:-}" ]]; then
        script="$THIS_SCRIPT"
    else
        script="${this_script:-$(basename "${BASH_SOURCE[0]:-$0}")}"

        # Check if the script name is valid; fallback to "temp_script.sh" if
        # it's bash or invalid
        if [[ "$script" == "bash" || -z "$script" ]]; then
            script="temp_script.sh"
        fi
    fi

    # Validate that the line number is set
    if [[ -z "$line" ]]; then
        line="unknown"
    fi

    # Log the error message to stderr
    printf "\n" >&2
    printf "[ERROR] Error in '%s()' at line %s of '%s'. Exiting.\n" \
        "$func" "$line" "$script" >&2

    # Exit with a non-zero status code
    exit 1
}

# Set the trap to call trap_error on any error by uncommenting the next
# line to help with script debugging.
# trap 'trap_error "$LINENO"' ERR

############
### Global Script Declarations
############

# -----------------------------------------------------------------------------
# @var REQUIRE_SUDO
# @brief Indicates whether root privileges are required to run the script.
# @details This variable determines if the script requires execution with root
#          privileges. It defaults to `true`, meaning the script will enforce
#          that it is run with `sudo` or as a root user. This behavior can be
#          overridden by setting the `REQUIRE_SUDO` environment variable to
#          `false`.
#
# @default true
#
# @example
# REQUIRE_SUDO=false ./install.sh  # Run the script without enforcing root
#                                     privileges.
# -----------------------------------------------------------------------------
readonly REQUIRE_SUDO="${REQUIRE_SUDO:-true}"

# -----------------------------------------------------------------------------
# @brief Determines the script name to use.
# @details This block of code determines the value of `THIS_SCRIPT` based on
#          the following logic:
#          1. If `THIS_SCRIPT` is already set in the environment, it is used.
#          2. If `THIS_SCRIPT` is not set, the script checks if
#             `${BASH_SOURCE[0]}` is available:
#             - If `${BASH_SOURCE[0]}` is set and not equal to `"bash"`, the
#               script extracts the filename (without the path) using
#               `basename` and assigns it to `THIS_SCRIPT`.
#             - If `${BASH_SOURCE[0]}` is unbound or equals `"bash"`, it falls
#               back to using the value of `FALLBACK_SCRIPT_NAME`, which
#               defaults to `debug_print.sh`.
#
# @var FALLBACK_SCRIPT_NAME
# @brief Default name for the script in case `BASH_SOURCE[0]` is unavailable.
# @details This variable is used as a fallback value if `BASH_SOURCE[0]` is
#          not set or equals `"bash"`. The default value is `"debug_print.sh"`.
#
# @var THIS_SCRIPT
# @brief Holds the name of the script to use.
# @details The script attempts to determine the name of the script to use. If
#          `THIS_SCRIPT` is already set in the environment, it is used
#          directly. Otherwise, the script tries to extract the filename from
#          `${BASH_SOURCE[0]}` (using `basename`). If that fails, it defaults
#          to `FALLBACK_SCRIPT_NAME`.
# -----------------------------------------------------------------------------
declare FALLBACK_SCRIPT_NAME="${FALLBACK_SCRIPT_NAME:-install.sh}"
if [[ -z "${THIS_SCRIPT:-}" ]]; then
    if [[ -n "${BASH_SOURCE[0]:-}" && "${BASH_SOURCE[0]:-}" != "bash" ]]; then
        # Use BASH_SOURCE[0] if it is available and not "bash"
        THIS_SCRIPT=$(basename "${BASH_SOURCE[0]}")
    else
        # If BASH_SOURCE[0] is unbound or equals "bash", use
        # FALLBACK_SCRIPT_NAME
        THIS_SCRIPT="${FALLBACK_SCRIPT_NAME}"
    fi
fi

# -----------------------------------------------------------------------------
# @var ACTION
# @brief Determines whether the script runs in "install" or "uninstall" mode.
# @details This variable controls whether the script installs or uninstalls
#          the systemd service. If not explicitly set, it defaults to
#          "install". The script checks this variable to execute the
#          appropriate actions.
#
# @default "install"
#
# @example
# ACTION="install"  # Set to install mode
# ACTION="uninstall"  # Set to uninstall mode
# -----------------------------------------------------------------------------
declare ACTION="${ACTION:-install}"

# -----------------------------------------------------------------------------
# @var DRY_RUN
# @brief Enables simulated execution of certain commands.
# @details When set to `true`, commands are not actually executed but are
#          simulated to allow testing or validation without side effects.
#          If set to `false`, commands execute normally.
#
# @example
# DRY_RUN=true ./install.sh  # Run the script in dry-run mode.
# -----------------------------------------------------------------------------
declare DRY_RUN="${DRY_RUN:-false}"

# Temporary build swap defaults to automatic allocation for low-memory builds;
# false opts out and true explicitly permits the same resource policy. The remaining
# BUILD_RESOURCE_* variables are test seams and reviewed policy constants; the
# installer does not expose them as operator controls.
declare ALLOW_TEMP_BUILD_SWAP="${ALLOW_TEMP_BUILD_SWAP:-auto}"
declare BUILD_RESOURCE_MEMINFO_PATH="/proc/meminfo"
declare BUILD_RESOURCE_SWAPS_PATH="/proc/swaps"
declare BUILD_RESOURCE_SWAP_ROOT="/var/lib/wsprrypi/build-swap"
declare BUILD_RESOURCE_DISK_AVAILABLE_KB=""
declare BUILD_RESOURCE_LOW_RAM_KB=$((1536 * 1024))
declare BUILD_RESOURCE_VERY_LOW_RAM_KB=$((768 * 1024))
declare BUILD_RESOURCE_LOW_RAM_SWAP_KB=$((1024 * 1024))
declare BUILD_RESOURCE_VERY_LOW_RAM_SWAP_KB=$((2048 * 1024))
declare BUILD_RESOURCE_DISK_RESERVE_KB=$((512 * 1024))
declare BUILD_RESOURCE_SWAP_METADATA_ALLOWANCE_KB=1024
declare BUILD_RESOURCE_SWAP_ALLOCATION_QUANTUM_KB=1024
declare TEMP_BUILD_SWAP_PATH=""
declare TEMP_BUILD_SWAP_OWNED="false"
declare TEMP_BUILD_SWAP_ACTIVE="false"
declare TEMP_BUILD_SWAP_ROOT_OWNED="false"

# -----------------------------------------------------------------------------
# @brief Defines global repository-related variables.
# @details These variables are used to determine the repository context,
#          including whether the script is running inside a local Git
#          repository or fetching data remotely. Default values are set
#          to ensure fallback behavior when values are unset.
#
# @global IS_REPO Boolean flag indicating if the script is running inside
#                 a local Git repository (default: false).
# @global REPO_ORG The GitHub organization or user associated with the repository.
# @global REPO_NAME The name of the GitHub repository.
# @global REPO_TITLE A human-readable title for the repository.
# @global REPO_BRANCH The default branch of the repository.
# @global GIT_TAG The default Git tag/version to use.
# @global GIT_RAW_BASE The base URL for raw file retrieval from GitHub.
# @global GIT_API_BASE The base URL for accessing GitHub's API.
# @global GIT_CLONE_BASE The base URL for cloning repositories from GitHub.
# -----------------------------------------------------------------------------
declare IS_REPO="${IS_REPO:-false}"
declare REPO_ORG="${REPO_ORG:-WsprryPi}"
declare REPO_NAME="WsprryPi"      # Case Sensitive
declare UI_REPO_DIR="WsprryPi-UI" # Case Sensitive
declare REPO_TITLE="${REPO_TITLE:-Wsprry Pi}"
declare REPO_BRANCH="${REPO_BRANCH:-$DEFAULT_REPO_BRANCH}"
declare SEM_VER="${SEM_VER:-$DEFAULT_SEM_VER}"
declare GIT_TAG="${GIT_TAG:-v$SEM_VER}"
declare GIT_RAW_BASE="https://raw.githubusercontent.com"
declare GIT_API_BASE="https://api.github.com/repos"
declare GIT_CLONE_BASE="https://github.com"
declare FAIL_ON_UI_MODIFICATIONS="${FAIL_ON_UI_MODIFICATIONS:-false}"
declare UI_PUBLICATION_ATTEMPTED="false"
declare UI_PUBLICATION_RESULT_FILE=""
declare INSTALL_RP1_GPCLK_DKMS="${INSTALL_RP1_GPCLK_DKMS:-auto}"
declare RP1_GPCLK_DKMS_SOURCE="${RP1_GPCLK_DKMS_SOURCE:-auto}"
declare REMOVE_RP1_GPCLK_DKMS="${REMOVE_RP1_GPCLK_DKMS:-auto}"
declare RP1_GPCLK_DKMS_STATE_DIR=""
declare RP1_GPCLK_DKMS_HELPER=""
declare RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN="false"
declare -a RESOLVED_APT_PACKAGES=()
declare BINARY_SOURCE="${BINARY_SOURCE:-build}"
declare BINARY_PATH="${BINARY_PATH:-}"
declare BINARY_RELEASE_TAG="${BINARY_RELEASE_TAG:-}"
declare BINARY_STAGE=""
declare BINARY_VERSION=""
declare -a BINARY_RUNTIME_PACKAGES=()
declare BINARY_PREVIOUS=""
declare BINARY_INSTALLED="false"
declare BINARY_DESTINATION=""
declare BINARY_INSTALL_DIR="/usr/local/bin"

# -----------------------------------------------------------------------------
# Declare Arguments Variables
# -----------------------------------------------------------------------------
declare ARGUMENTS_LIST=() # List of word arguments for command line parsing
declare OPTIONS_LIST=()   # List of -f--fl arguments for command line parsing

# -----------------------------------------------------------------------------
# @var GIT_DIRS
# @brief List of relevant directories for download.
# @details This array contains the names of the directories within the GitHub
#          repository that will be processed and downloaded. The directories
#          include 'man', 'scripts', and 'conf'. These directories are used
#          in the script to determine which content to fetch from the
#          repository.
# -----------------------------------------------------------------------------
readonly GIT_DIRS="${GIT_DIRS:-("config" "WsprryPi-UI/data" "executables" "systemd")}"

# -----------------------------------------------------------------------------
# @var WSPR_SERVICE
# @brief The systemd service name for WsprryPi.
# @details This variable holds the name of the main WsprryPi service that
#          will be installed in `/etc/systemd/system/`.
#
# @var NON_MAIN_AS_RELEASE
# @brief Controls whether non-main branches build as release.
# @details When set to `"true"`, branches other than `main` or `master`
#          build as release and use the standard executable name. When set to
#          `"false"`, non-main branches follow the default build policy unless
#          overridden explicitly.
#
# @var BUILD_TYPE_OVERRIDE
# @brief Optional explicit build type override.
# @details When set to `"DEBUG"` or `"RELEASE"`, this overrides the branch-
#          based build selection logic.
#
# @var WSPR_BUILD_TYPE
# @brief The resolved build type for WsprryPi.
# @details This value is resolved by `resolve_build_settings()`.
#
# @var WSPR_EXE
# @brief The executable name for WsprryPi.
# @details This variable holds the name of the main WsprryPi executable that
#          will be installed in `/usr/local/bin/` and used for signal
#          processing. Debug builds use the `_debug` suffix.
#
# @var WSPR_INI
# @brief The configuration file for WsprryPi.
# @details This variable specifies the name of the WsprryPi configuration file,
#          typically stored in `/usr/local/etc/`, containing user settings.
# -----------------------------------------------------------------------------
readonly WSPR_SERVICE="wsprrypi"
declare NON_MAIN_AS_RELEASE="${NON_MAIN_AS_RELEASE:-false}"
declare BUILD_TYPE_OVERRIDE="${BUILD_TYPE_OVERRIDE:-}"
declare WSPR_BUILD_TYPE=""
declare WSPR_EXE=""
readonly WSPR_INI="wsprrypi.ini"

# -----------------------------------------------------------------------------
# @brief Resolve the effective build type and executable name.
# @details Applies `BUILD_TYPE_OVERRIDE` first. Otherwise, defaults to release
#          builds. Non-main branch behavior may still be controlled with
#          `NON_MAIN_AS_RELEASE` if desired.
#
# @global BUILD_TYPE_OVERRIDE Optional explicit build override.
# @global NON_MAIN_AS_RELEASE Whether non-main branches should build as release.
# @global REPO_BRANCH The selected repository branch.
# @global WSPR_BUILD_TYPE The resolved build type.
# @global WSPR_EXE The resolved executable name.
#
# @param $@ Optional debug flag.
#
# @return 0 on success.
# -----------------------------------------------------------------------------
resolve_build_settings() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    local branch
    local build_type

    branch="${REPO_BRANCH:-main}"
    build_type="${BUILD_TYPE_OVERRIDE^^}"

    if [[ -n "${build_type}" ]]; then
        case "${build_type}" in
            DEBUG|RELEASE)
                ;;
            *)
                printf "%s\n" \
                    "Error: BUILD_TYPE_OVERRIDE must be DEBUG or RELEASE." >&2
                debug_end "$debug"
                return 1
                ;;
        esac
    else
        build_type="RELEASE"

        if [[ "${branch}" != "main" && "${branch}" != "master" ]]; then
            if [[ "${NON_MAIN_AS_RELEASE}" != "true" ]]; then
                build_type="RELEASE"
            fi
        fi
    fi

    WSPR_BUILD_TYPE="${build_type}"

    if [[ "${WSPR_BUILD_TYPE}" == "DEBUG" ]]; then
        WSPR_EXE="${WSPR_SERVICE}_debug"
    else
        WSPR_EXE="${WSPR_SERVICE}"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @var USER_HOME
# @brief Home directory of the current user.
# @details This variable stores the home directory of the user executing the script.
#          If the script is run with `sudo`, it uses the home directory of the
#          `SUDO_USER`, otherwise, it defaults to the current user's home directory.
# -----------------------------------------------------------------------------
declare USER_HOME
if [[ "$REQUIRE_SUDO" == true && -z "${SUDO_USER-}" ]]; then
    # Fail gracefully if REQUIRE_SUDO is true and SUDO_USER is not set
    USER_HOME=""
elif [[ -n "${SUDO_USER-}" ]]; then
    # Use SUDO_USER's home directory if it's set
    USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
else
    # Fallback to HOME if SUDO_USER is not set
    USER_HOME="$HOME"
fi

# -----------------------------------------------------------------------------
# @var USE_CONSOLE
# @brief Controls whether logging output is directed to the console.
# @details When set to `true`, log messages are displayed on the console in
#          addition to being written to the log file (if enabled). When set
#          to `false`, log messages are written only to the log file, making
#          it suitable for non-interactive or automated environments.
#
# @example
# - USE_CONSOLE=true: Logs to both console and file.
# - USE_CONSOLE=false: Logs only to file.
# -----------------------------------------------------------------------------
declare USE_CONSOLE="${USE_CONSOLE:-true}"

# -----------------------------------------------------------------------------
# @var CONSOLE_STATE
# @brief Tracks the original console logging state.
# @details This variable mirrors the value of USE_CONSOLE and provides a
#          consistent reference for toggling or querying the state of console
#          logging.
#
# @example
# - CONSOLE_STATE="USE_CONSOLE": Console logging matches USE_CONSOLE.
# -----------------------------------------------------------------------------
declare CONSOLE_STATE="${CONSOLE_STATE:-$USE_CONSOLE}"

# -----------------------------------------------------------------------------
# @var TERSE
# @brief Enables or disables terse logging mode.
# @details When `TERSE` is set to `true`, log messages are minimal and
#          optimized for automated environments where concise output is
#          preferred. When set to `false`, log messages are verbose, providing
#          detailed information suitable for debugging or manual intervention.
#
# @example
# TERSE=true  # Enables terse logging mode.
# ./install.sh
#
# TERSE=false # Enables verbose logging mode.
# ./install.sh
# -----------------------------------------------------------------------------
declare TERSE="${TERSE:-true}"

# -----------------------------------------------------------------------------
# @var REQUIRE_INTERNET
# @type string
# @brief Flag indicating if internet connectivity is required.
# @details Controls whether the script should verify internet connectivity
#          during initialization. This variable can be overridden by setting
#          the `REQUIRE_INTERNET` environment variable before running the
#          script.
#
# @values
# - `"true"`: Internet connectivity is required.
# - `"false"`: Internet connectivity is not required.
#
# @default "true"
#
# @example
# REQUIRE_INTERNET=false ./install.sh  # Run the script without verifying
#                                       # Internet connectivity.
# -----------------------------------------------------------------------------
readonly REQUIRE_INTERNET="${REQUIRE_INTERNET:-true}"

# -----------------------------------------------------------------------------
# @var MIN_BASH_VERSION
# @brief Specifies the minimum supported Bash version.
# @details Defines the minimum Bash version required to execute the script. By
#          default, it is set to `4.0`. This value can be overridden by setting
#          the `MIN_BASH_VERSION` environment variable before running the
#          script.
#          To disable version checks entirely, set this variable to `"none"`.
#
# @default "4.0"
#
# @example
# MIN_BASH_VERSION="none" ./install.sh  # Disable Bash version checks.
# MIN_BASH_VERSION="5.0" ./install.sh   # Require at least Bash 5.0.
# -----------------------------------------------------------------------------
readonly MIN_BASH_VERSION="${MIN_BASH_VERSION:-4.0}"

# -----------------------------------------------------------------------------
# @var MIN_OS
# @brief Specifies the minimum supported OS version.
# @details Defines the lowest OS version that the script supports. This value
#          should be updated as compatibility requirements evolve. It is used
#          to ensure the script is executed only on compatible systems.
#
# @default 11
#
# @example
# if [[ "$CURRENT_OS_VERSION" -lt "$MIN_OS" ]]; then
#     echo "This script requires OS version $MIN_OS or higher."
#     exit 1
# fi
# -----------------------------------------------------------------------------
readonly MIN_OS="${MIN_OS:-11}"

# -----------------------------------------------------------------------------
# @var MAX_OS
# @brief Specifies the maximum supported OS version.
# @details Defines the highest OS version that the script supports. If the
#          script is executed on a system with an OS version higher than this
#          value, it may not function as intended. Set this to `-1` to indicate
#          no upper limit on supported OS versions.
#
# @default 15
#
# @example
# if [[ "$CURRENT_OS_VERSION" -gt "$MAX_OS" && "$MAX_OS" -ne -1 ]]; then
#     echo "This script supports OS versions up to $MAX_OS."
#     exit 1
# fi
# -----------------------------------------------------------------------------
readonly MAX_OS="${MAX_OS:-13}" # (use -1 for no upper limit)

# -----------------------------------------------------------------------------
# @var SUPPORTED_BITNESS
# @brief Specifies the supported system bitness.
# @details Defines the system architectures that the script supports.
#          Acceptable values are:
#          - `"32"`: Only supports 32-bit systems.
#          - `"64"`: Only supports 64-bit systems.
#          - `"both"`: Supports both 32-bit and 64-bit systems.
#          This variable ensures compatibility with the intended system architecture.
#          It defaults to `"32"` if not explicitly set.
#
# @default "32"
#
# @example
# if [[ "$BITNESS" != "$SUPPORTED_BITNESS" && \
#     "$SUPPORTED_BITNESS" != "both" ]]; then
#     echo "This script supports $SUPPORTED_BITNESS-bit systems only."
#     exit 1
# fi
# -----------------------------------------------------------------------------
readonly SUPPORTED_BITNESS="${SUPPORTED_BITNESS:-both}" # ("32", "64", or "both")

# -----------------------------------------------------------------------------
# @var SUPPORTED_MODELS
# @brief Associative array of Raspberry Pi models and their support statuses.
# @details This associative array maps Raspberry Pi model identifiers to their
#          corresponding support statuses. Each key is a pipe-delimited string
#          containing:
#          - The model name (e.g., "Raspberry Pi 4 Model B").
#          - A simplified identifier (e.g., "4-model-b").
#          - The chipset identifier (e.g., "bcm2711").
#
#          The value is the support status, which can be:
#          - `"Supported"`: Indicates the model is supported by the script.
#          - `"Not Supported"`: Indicates the model is not supported.
#
#          This array is marked as `readonly` to ensure it remains immutable at
#          runtime.
#
#          declare SUPPORTED_MODELS="all" at invocation or in the environment
#          to accept any models.
#
# @example
# for model in "${!SUPPORTED_MODELS[@]}"; do
#     IFS="|" read -r full_name short_name chipset <<< "$model"
#     echo "Model: $full_name ($short_name, $chipset) - Status: \
# ${SUPPORTED_MODELS[$model]}"
# done
# -----------------------------------------------------------------------------
# Ensure SUPPORTED_MODELS is initialized
declare -A SUPPORTED_MODELS

# Safely check if SUPPORTED_MODELS is empty
if [[ -z "${SUPPORTED_MODELS+x}" || ${#SUPPORTED_MODELS[@]} -eq 0 ]]; then
    # If SUPPORTED_MODELS is not set or empty, define the default supported models
    SUPPORTED_MODELS=(
        # Supported models
        ["Raspberry Pi 5|5-model-b|bcm2712"]="Supported"
        ["Raspberry Pi Compute Module 5|5-compute-module|bcm2712"]="Supported"
        ["Raspberry Pi 400|400|bcm2711"]="Supported"
        ["Raspberry Pi Compute Module 4S|4s-compute-module|bcm2711"]="Supported"
        ["Raspberry Pi Compute Module 4|4-compute-module|bcm2711"]="Supported"
        ["Raspberry Pi Compute Module 3+|3-plus-compute-module|bcm2837"]="Supported"
        ["Raspberry Pi Compute Module 3|3-compute-module|bcm2837"]="Supported"
        ["Raspberry Pi Compute Module Zero|0-compute-module|bcm2837"]="Supported"
        ["Raspberry Pi Compute Module|compute-module|bcm2835"]="Supported"
        ["Raspberry Pi 4 Model B|4-model-b|bcm2711"]="Supported"
        ["Raspberry Pi 3 Model A+|3-model-a-plus|bcm2837"]="Supported"
        ["Raspberry Pi 3 Model B+|3-model-b-plus|bcm2837"]="Supported"
        ["Raspberry Pi 3 Model B|3-model-b|bcm2837"]="Supported"
        ["Raspberry Pi 2 Model B|2-model-b|bcm2836"]="Supported"
        ["Raspberry Pi Model A+|model-a-plus|bcm2835"]="Supported"
        ["Raspberry Pi Model B+|model-b-plus|bcm2835"]="Supported"
        ["Raspberry Pi Model B Rev 2|model-b-rev2|bcm2835"]="Supported"
        ["Raspberry Pi Model A|model-a|bcm2835"]="Supported"
        ["Raspberry Pi Model B|model-b|bcm2835"]="Supported"
        ["Raspberry Pi Zero 2 W|model-zero-2-w|bcm2837"]="Supported"
        ["Raspberry Pi Zero|model-zero|bcm2835"]="Supported"
        ["Raspberry Pi Zero W|model-zero-w|bcm2835"]="Supported"
    )
elif [[ ${SUPPORTED_MODELS[all]+_} ]]; then
    # If SUPPORTED_MODELS contains "all", set it as a special value
    SUPPORTED_MODELS=(["all"]="Supported")
fi

# -----------------------------------------------------------------------------
# @var LOG_OUTPUT
# @brief Controls where log messages are directed.
# @details Specifies the logging destination(s) for the script's output. This
#          variable can be set to one of the following values:
#          - `"file"`: Log messages are written only to a file.
#          - `"console"`: Log messages are displayed only on the console.
#          - `"both"`: Log messages are written to both the console and a file.
#          - `unset`: Defaults to `"both"`.
#
#          This variable allows flexible logging behavior depending on the
#          environment or use case.
#
# @default "both"
#
# @example
# LOG_OUTPUT="file" ./install.sh      # Logs to a file only.
# LOG_OUTPUT="console" ./install.sh   # Logs to the console only.
# LOG_OUTPUT="both" ./install.sh      # Logs to both destinations.
# -----------------------------------------------------------------------------
declare LOG_OUTPUT="${LOG_OUTPUT:-both}"

# -----------------------------------------------------------------------------
# @var LOG_FILE
# @brief Specifies the path to the log file.
#
# @details Uses the environment variable LOG_FILE if set; otherwise defaults
#          to "$USER_HOME/$WSPR_SERVICE.log", where USER_HOME is the user's home
#          directory and WSPR_SERVICE is the executable name.
#
# @example
# LOG_FILE="/var/log/my_script.log" ./install.sh  # Use a custom log file.
# -----------------------------------------------------------------------------
declare LOG_FILE="${LOG_FILE:-$USER_HOME/$WSPR_SERVICE.log}"

# -----------------------------------------------------------------------------
# @var LOG_LEVEL
# @brief Specifies the logging verbosity level.
# @details Defines the verbosity level for logging messages. This variable
#          controls which messages are logged based on their severity.
#          It defaults to:
#            - `"INFO"` for release builds.
#            - `"DEBUG"` for debug builds.
#
# @default "INFO" for release builds; "DEBUG" for debug builds
#
# @example
# LOG_LEVEL="ERROR" ./install.sh  # Force log level to ERROR.
# -----------------------------------------------------------------------------
declare LOG_LEVEL_SET_FROM_ENV="false"
if [[ -n "${LOG_LEVEL+x}" ]]; then
    LOG_LEVEL_SET_FROM_ENV="true"
else
    LOG_LEVEL="$(if [[ "${WSPR_BUILD_TYPE}" == "RELEASE" ]]; then
        printf "%s
" "INFO"
    else
        printf "%s
" "DEBUG"
    fi)"
fi
declare LOG_LEVEL

# -----------------------------------------------------------------------------
# @var DEPENDENCIES
# @type array
# @brief List of required external commands for the script.
# @details This array defines the external commands that the script depends on
#          to function correctly. Each command in this list is checked for
#          availability at runtime. If a required command is missing, the script
#          may fail or display an error message.
#
#          Best practices:
#          - Ensure all required commands are included.
#          - Use a dependency-checking function to verify their presence early
#            in the script.
#
# @default
# A predefined set of common system utilities
#
# @note Update this list as needed to reflect the actual commands used in the script.
#
# @example
# for cmd in "${DEPENDENCIES[@]}"; do
#     if ! command -v "$cmd" &>/dev/null; then
#         echo "Error: Missing required command: $cmd"
#         exit 1
#     fi
# done
# -----------------------------------------------------------------------------
declare -ar DEPENDENCIES=(
    "awk"
    "grep"
    "tput"
    "cut"
    "tr"
    "getconf"
    "cat"
    "sed"
    "basename"
    "getent"
    "date"
    "printf"
    "whoami"
    "touch"
    "dpkg"
    "dpkg-deb"
    "dpkg-reconfigure"
    "python3"
    "curl"
    "wget"
    "realpath"
)
readonly DEPENDENCIES

# -----------------------------------------------------------------------------
# @var ENV_VARS_BASE
# @type array
# @brief Base list of required environment variables.
# @details Defines the core environment variables that the script relies on,
#          regardless of the runtime context. These variables must be set to
#          ensure the script functions correctly.
#
#          - `HOME`: Specifies the home directory of the current user.
#          - `COLUMNS`: Defines the width of the terminal, used for formatting.
#
# @example
# for var in "${ENV_VARS_BASE[@]}"; do
#     if [[ -z "${!var}" ]]; then
#         echo "Error: Required environment variable '$var' is not set."
#         exit 1
#     fi
# done
# -----------------------------------------------------------------------------
declare -ar ENV_VARS_BASE=(
    "HOME"    # Home directory of the current user
    "COLUMNS" # Terminal width for formatting, often dynamic in the OS
)

# -----------------------------------------------------------------------------
# @var ENV_VARS
# @type array
# @brief Final list of required environment variables.
# @details This array extends `ENV_VARS_BASE` to include additional variables
#          required under specific conditions. If the script requires root
#          privileges (`REQUIRE_SUDO=true`), the `SUDO_USER` variable is added
#          dynamically during runtime. Otherwise, it inherits only the base
#          environment variables.
#
#          - `SUDO_USER`: Identifies the user who invoked the script using `sudo`.
#
# @note Ensure `ENV_VARS_BASE` is properly defined before constructing `ENV_VARS`.
#
# @example
# for var in "${ENV_VARS[@]}"; do
#     if [[ -z "${!var}" ]]; then
#         echo "Error: Required environment variable '$var' is not set."
#         exit 1
#     fi
# done
# -----------------------------------------------------------------------------
if [[ "$REQUIRE_SUDO" == true ]]; then
    readonly -a ENV_VARS=("${ENV_VARS_BASE[@]}" "SUDO_USER")
else
    readonly -a ENV_VARS=("${ENV_VARS_BASE[@]}")
fi

# -----------------------------------------------------------------------------
# @var COLUMNS
# @brief Terminal width in columns.
# @details The `COLUMNS` variable represents the width of the terminal in
#          characters. It is used for formatting output to fit within the
#          terminal's width. If not already set by the environment, it defaults
#          to `80` columns. This value can be overridden externally by setting
#          the `COLUMNS` environment variable before running the script.
#
# @default 80
#
# @example
# echo "The terminal width is set to $COLUMNS columns."
# -----------------------------------------------------------------------------
COLUMNS="${COLUMNS:-80}"

# -----------------------------------------------------------------------------
# @var SYSTEM_READS
# @type array
# @brief List of critical system files to check.
# @details Defines the absolute paths to system files that the script depends on
#          for its execution. These files must be present and readable to ensure
#          the script operates correctly. The following files are included:
#          - `/etc/os-release`: Contains operating system identification data.
#          - `/proc/device-tree/compatible`: Identifies hardware compatibility,
#            commonly used in embedded systems like Raspberry Pi.
#
# @example
# for file in "${SYSTEM_READS[@]}"; do
#     if [[ ! -r "$file" ]]; then
#         echo "Error: Required system file '$file' is missing or not readable."
#         exit 1
#     fi
# done
# -----------------------------------------------------------------------------
declare -ar SYSTEM_READS=(
    "/etc/os-release"              # OS identification file
    "/proc/device-tree/compatible" # Hardware compatibility file
)
readonly SYSTEM_READS

# -----------------------------------------------------------------------------
# Runtime, web, application-build, and optional kernel-build dependencies are
# selected separately. Precompiled installation does not need application headers
# or a compiler. age and age-keygen support encrypted diagnostic bundles.
# -----------------------------------------------------------------------------
readonly RUNTIME_APT_PACKAGES=(
    "git"
    "python3"
    "chrony"
    "age"
    "libatomic1"
    "libstdc++6"
    "libgcc-s1"
    "libc6"
    "libsystemd0"
)
readonly WEB_APT_PACKAGES=("apache2" "php")
readonly BUILD_APT_PACKAGES=(
    "build-essential"
    "libgpiod-dev"
    "libsystemd-dev"
    "libssl-dev"
)
# readelf inspects supplied files; it does not compile or execute WsprryPi.
readonly BINARY_VALIDATION_APT_PACKAGES=("binutils")

# RP1 kernel-build packages are appended only when RP1 provider installation is
# selected. The exact running-kernel header package is resolved at runtime.
readonly RP1_GPCLK_DKMS_APT_PACKAGES=(
    "build-essential"
    "dkms"
    "device-tree-compiler"
    "kmod"
)

# -----------------------------------------------------------------------------
# @var WARN_STACK_TRACE
# @type string
# @brief Flag to enable stack trace logging for warnings.
# @details Controls whether stack traces are printed alongside warning
#          messages. This feature is particularly useful for debugging and
#          tracking the script's execution path in complex workflows.
#
#          Possible values:
#          - `"true"`: Enables stack trace logging for warnings.
#          - `"false"`: Disables stack trace logging for warnings (default).
#
# @default "false"
#
# @example
# WARN_STACK_TRACE=true ./install.sh  # Enable stack traces for warnings.
# WARN_STACK_TRACE=false ./install.sh # Disable stack traces for warnings.
# -----------------------------------------------------------------------------
readonly WARN_STACK_TRACE="${WARN_STACK_TRACE:-false}"

# -----------------------------------------------------------------------------
# @var DEFAULT_TARGET_FILE
# @type string
# @brief Default Apache landing page path.
# @details Path to the HTML file used to detect the stock Apache welcome page
#          before applying any custom configurations.
# @default "/var/www/html/index.html"
# @example DEFAULT_TARGET_FILE="/custom/index.html" ./install.sh
# -----------------------------------------------------------------------------
declare DEFAULT_TARGET_FILE="/var/www/html/index.html"

# -----------------------------------------------------------------------------
# @var DEFAULT_APACHE_CONF
# @type string
# @brief Default Apache configuration file path.
# @details Path to the primary Apache2 configuration file where global directives
#          (like ServerName) are managed.
# @default "/etc/apache2/apache2.conf"
# @example DEFAULT_APACHE_CONF="/etc/apache2/custom.conf" ./install.sh
# -----------------------------------------------------------------------------
declare DEFAULT_APACHE_CONF="/etc/apache2/apache2.conf"

# -----------------------------------------------------------------------------
# @var DEFAULT_LOG_FILE
# @type string
# @brief Default log file for installer operations.
# @details Path where the script writes its operational logs if logging is enabled.
# @default "/var/log/apache_tool.log"
# @example DEFAULT_LOG_FILE="/var/log/custom_installer.log" ./install.sh
# -----------------------------------------------------------------------------
# shellcheck disable=SC2034
declare DEFAULT_LOG_FILE="/var/log/apache_tool.log"

# -----------------------------------------------------------------------------
# @var DEFAULT_SITES_CONF
# @brief Default directory for Apache site configuration files.
# @details
#   Holds the path to the Apache “sites-available” directory where
#   virtual host configuration files are stored.
# -----------------------------------------------------------------------------
declare DEFAULT_SITES_CONF="/etc/apache2/sites-available"

# -----------------------------------------------------------------------------
# @var DEFAULT_SERVERNAME
# @type string
# @brief Default ServerName directive.
# @details The ServerName line to insert into apache2.conf to suppress FQDN warnings.
# @default "ServerName localhost"
# @example DEFAULT_SERVERNAME="ServerName myhost.local" ./install.sh
# -----------------------------------------------------------------------------
declare DEFAULT_SERVERNAME="ServerName localhost"

# -----------------------------------------------------------------------------
# @var TARGET_FILE
# @type string
# @brief Effective target file for stock-page detection.
# @details Overrides DEFAULT_TARGET_FILE if set in the environment; otherwise
#          falls back to DEFAULT_TARGET_FILE.
# @default value of DEFAULT_TARGET_FILE
# @example TARGET_FILE="/custom/index.html" ./install.sh
# -----------------------------------------------------------------------------
declare TARGET_FILE="${TARGET_FILE:-$DEFAULT_TARGET_FILE}"

# -----------------------------------------------------------------------------
# @var APACHE_CONF
# @type string
# @brief Effective Apache configuration file path.
# @details Overrides DEFAULT_APACHE_CONF if set in the environment; otherwise
#          falls back to DEFAULT_APACHE_CONF.
# @default value of DEFAULT_APACHE_CONF
# @example APACHE_CONF="/etc/apache2/custom.conf" ./install.sh
# -----------------------------------------------------------------------------
declare APACHE_CONF="${APACHE_CONF:-$DEFAULT_APACHE_CONF}"

# -----------------------------------------------------------------------------
# @var SERVERNAME_DIRECTIVE
# @type string
# @brief Effective ServerName directive.
# @details Overrides DEFAULT_SERVERNAME if set in the environment; otherwise
#          falls back to DEFAULT_SERVERNAME.
# @default value of DEFAULT_SERVERNAME
# @example SERVERNAME_DIRECTIVE="ServerName example.com" ./install.sh
# -----------------------------------------------------------------------------
declare SERVERNAME_DIRECTIVE="${SERVERNAME_DIRECTIVE:-$DEFAULT_SERVERNAME}"

# -----------------------------------------------------------------------------
# @var APACHE_WEB_MODE
# @type string
# @brief Detected Apache integration mode for the current system.
# @details Populated by `detect_apache_web_mode()` during the preflight phase
#          and reused by `manage_apache()` during the action phase.
# @values stock | existing_wsprrypi | custom_site | no_apache | unknown
# -----------------------------------------------------------------------------
declare APACHE_WEB_MODE="${APACHE_WEB_MODE:-unknown}"

# -----------------------------------------------------------------------------
# @var APACHE_WEB_MODE_DETECTED
# @type bool
# @brief Indicates whether Apache mode detection has already been performed.
# -----------------------------------------------------------------------------
declare APACHE_WEB_MODE_DETECTED="${APACHE_WEB_MODE_DETECTED:-false}"

# -----------------------------------------------------------------------------
# @var NO_WEB
# @type bool
# @brief Disable installation and runtime configuration of the web UI.
# @details When set to `true`, the installer skips web asset and Apache
#          integration steps and configures the service to start with `--no-web`.
# -----------------------------------------------------------------------------
declare NO_WEB="${NO_WEB:-false}"

# -----------------------------------------------------------------------------
# @var WAS_RUNNING
# @type bool
# @brief Semaphore indicating if the service was running before invocation.
# @details
#   Set to true if the target service was active at the start of the script.
#   Used to decide whether to restart or stop the service during cleanup.
# @default false
# @example
#   # Check and record service status before changes
#   WAS_RUNNING=$(systemctl is-active --quiet myservice && echo true || echo false)
# -----------------------------------------------------------------------------
declare WAS_RUNNING="false"

# -----------------------------------------------------------------------------
# @var REBOOT
# @brief Indicates whether a system reboot is required.
# @details
#   Acts as a semaphore flag. Defaults to "false" if not set.
#   Operations that require a reboot must set this to "true".
# -----------------------------------------------------------------------------
declare REBOOT=${REBOOT:-false}

############
### Standard Functions
############

# -----------------------------------------------------------------------------
# @brief Handles shell exit operations, displaying session statistics.
# @details This function is called automatically when the shell exits. It
#          calculates and displays the number of commands executed during
#          the session and the session's end timestamp. It is intended to
#          provide users with session statistics before the shell terminates.
#
# @global EXIT This signal is trapped to call the `egress` function upon shell
#              termination.
#
# @note The function uses `history | wc -l` to count the commands executed in
#       the current session and `date` to capture the session end time.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
egress() {
    local status=$?
    if [[ "$status" -ne 0 ]]; then
        rollback_executable || status=1
        restore_daemon_state || status=1
    fi
    cleanup_precompiled_executable
    report_ui_publication_result
    cleanup_rp1_gpclk_dkms_state
    if ! cleanup_temp_build_swap; then
        if [[ "$status" -eq 0 ]]; then
            status=1
        fi
    fi
    trap - EXIT
    exit "$status"
}

# -----------------------------------------------------------------------------
# @brief Starts the debug process.
# @details This function checks if the "debug" flag is present in the
#          arguments, and if so, prints the debug information including the
#          function name, the caller function name, and the line number where
#          the function was called.
#
# @param "$@" Arguments to check for the "debug" flag.
#
# @return Returns the "debug" flag if present, or an empty string if not.
#
# @example
# debug_start "debug"  # Prints debug information
# debug_start          # Does not print anything, returns an empty string
# -----------------------------------------------------------------------------
debug_start() {
    local debug=""
    local args=() # Array to hold non-debug arguments

    # Look for the "debug" flag in the provided arguments
    for arg in "$@"; do
        if [[ "$arg" == "debug" ]]; then
            debug="debug"
            break # Exit the loop as soon as we find "debug"
        fi
    done

    # Handle empty or unset FUNCNAME and BASH_LINENO gracefully
    local this_script
    this_script=$(basename "${THIS_SCRIPT:-main}")
    this_script="${this_script%.*}"
    local func_name="${FUNCNAME[1]:-main}"
    local caller_name="${FUNCNAME[2]:-main}"
    local caller_line=${BASH_LINENO[1]:-0}
    local current_line=${BASH_LINENO[0]:-0}

    # Print debug information if the flag is set
    if [[ "$debug" == "debug" ]]; then
        printf "[DEBUG]\t[%s:%s():%d] Starting function called by %s():%d.\n" \
            "$this_script" "$func_name" "$current_line" "$caller_name" "$caller_line" >&2
    fi

    # Return the debug flag if present, or an empty string if not
    printf "%s\n" "${debug:-}"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Filters out the "debug" flag from the arguments.
# @details This function removes the "debug" flag from the list of arguments
#          and returns the filtered arguments. The debug flag is not passed
#          to other functions to avoid unwanted debug outputs.
#
# @param "$@" Arguments to filter.
#
# @return Returns a string of filtered arguments, excluding "debug".
#
# @example
# debug_filter "arg1" "debug" "arg2"  # Returns "arg1 arg2"
# -----------------------------------------------------------------------------
debug_filter() {
    local args=()  # Collect all non-debug, non-empty args

    for arg in "$@"; do
        if [[ -n "$arg" && "$arg" != "debug" ]]; then
            args+=("$arg")
        fi
    done

    # Emit them safely quoted for eval/set
    printf '%q ' "${args[@]}"
}

# -----------------------------------------------------------------------------
# @brief Prints a debug message if the debug flag is set.
# @details This function checks if the "debug" flag is present in the
#          arguments. If the flag is present, it prints the provided debug
#          message along with the function name and line number from which the
#          function was called.
#
# @param "$@" Arguments to check for the "debug" flag and the debug message.
# @global debug A flag to indicate whether debug messages should be printed.
#
# @return None.
#
# @example
# debug_print "debug" "This is a debug message"
# -----------------------------------------------------------------------------
debug_print() {
    local debug=""
    local args=() # Array to hold non-debug arguments

    # Loop through all arguments to identify the "debug" flag
    for arg in "$@"; do
        if [[ "$arg" == "debug" ]]; then
            debug="debug"
        else
            args+=("$arg") # Add non-debug arguments to the array
        fi
    done

    # Restore the positional parameters with the filtered arguments
    set -- "${args[@]}"

    # Handle empty or unset FUNCNAME and BASH_LINENO gracefully
    local this_script
    this_script=$(basename "${THIS_SCRIPT:-main}")
    this_script="${this_script%.*}"
    local caller_name="${FUNCNAME[1]:-main}"
    local caller_line="${BASH_LINENO[0]:-0}"

    # Assign the remaining argument to the message, defaulting to <unset>
    local message="${1:-<unset>}"

    # Print debug information if the debug flag is set
    if [[ "$debug" == "debug" ]]; then
        printf "[DEBUG]\t[%s:%s:%d] '%s'.\n" \
            "$this_script" "$caller_name" "$caller_line" "$message" >&2
    fi
}

# -----------------------------------------------------------------------------
# @brief Ends the debug process.
# @details This function checks if the "debug" flag is present in the
#          arguments. If the flag is present, it prints debug information
#          indicating the exit of the function, along with the function name
#          and line number from where the function was called.
#
# @param "$@" Arguments to check for the "debug" flag.
# @global debug Debug flag, passed from the calling function.
#
# @return None
#
# @example
# debug_end "debug"
# -----------------------------------------------------------------------------
debug_end() {
    local debug=""
    local args=() # Array to hold non-debug arguments

    # Loop through all arguments and identify the "debug" flag
    for arg in "$@"; do
        if [[ "$arg" == "debug" ]]; then
            debug="debug"
            break # Exit the loop as soon as we find "debug"
        fi
    done

    # Handle empty or unset FUNCNAME and BASH_LINENO gracefully
    local this_script
    this_script=$(basename "${THIS_SCRIPT:-main}")
    this_script="${this_script%.*}"
    local func_name="${FUNCNAME[1]:-main}"
    local caller_name="${FUNCNAME[2]:-main}"
    local caller_line=${BASH_LINENO[1]:-0}
    local current_line=${BASH_LINENO[0]:-0}

    # Print debug information if the flag is set
    if [[ "$debug" == "debug" ]]; then
        printf "[DEBUG]\t[%s:%s():%d] Exiting function returning to %s():%d.\n" \
            "$this_script" "$func_name" "$current_line" "$caller_name" "$caller_line" >&2
    fi
}

# -----------------------------------------------------------------------------
# @brief Prints a stack trace with optional formatting and a message.
# @details This function generates and displays a formatted stack trace for
#          debugging purposes. It includes a log level and optional details,
#          with color-coded formatting and proper alignment.
#
# @param $1 [optional] Log level (DEBUG, INFO, WARN, ERROR, CRITICAL).
#           Defaults to INFO.
# @param $2 [optional] Primary message for the stack trace.
# @param $@ [optional] Additional context or details for the stack trace.
#
# @global FUNCNAME Array of function names in the call stack.
# @global BASH_LINENO Array of line numbers corresponding to the call stack.
# @global THIS_SCRIPT The name of the current script, used for logging.
# @global COLUMNS Console width, used for formatting output.
#
# @throws None.
#
# @return None. Outputs the stack trace and message to standard output.
#
# @example
# stack_trace WARN "Unexpected condition detected."
# -----------------------------------------------------------------------------
stack_trace() {
    # Determine log level and message
    local level="${1:-INFO}"
    local message=""
    # Block width and character for header/footer
    local char="-"

    # Recalculate terminal columns
    COLUMNS=$( (command -v tput >/dev/null && tput cols) || printf "80")
    COLUMNS=$((COLUMNS > 0 ? COLUMNS : 80)) # Ensure COLUMNS is a positive number
    local width
    width=${COLUMNS:-80} # Max console width

    # Check if $1 is a valid level, otherwise treat it as the message
    case "$level" in
    DEBUG | INFO | WARN | WARNING | ERROR | CRIT | CRITICAL)
        shift
        ;;
    *)
        message="$level"
        level="INFO"
        shift
        ;;
    esac

    # Concatenate all remaining arguments into $message
    for arg in "$@"; do
        message+="$arg "
    done
    # Trim leading/trailing whitespace
    message=$(printf "%s" "$message" | xargs)

    # Define functions to skip
    local skip_functions=("die" "warn" "stack_trace")
    local encountered_main=0 # Track the first occurrence of main()

    # Generate title case function name for the banner
    local raw_function_name="${FUNCNAME[0]}"
    local header_name header_level
    header_name=$(printf "%s" "$raw_function_name" | sed -E 's/_/ /g; s/\b(.)/\U\1/g; s/(\b[A-Za-z])([A-Za-z]*)/\1\L\2/g')
    header_level=$(printf "%s" "$level" | sed -E 's/\b(.)/\U\1/g; s/(\b[A-Za-z])([A-Za-z]*)/\1\L\2/g')
    header_name="$header_level $header_name"

    # Helper: Skip irrelevant functions
    should_skip() {
        local func="$1"
        for skip in "${skip_functions[@]}"; do
            if [[ "$func" == "$skip" ]]; then
                return 0
            fi
        done
        if [[ "$func" == "main" && $encountered_main -gt 0 ]]; then
            return 0
        fi
        [[ "$func" == "main" ]] && ((encountered_main++))
        return 1
    }

    # Build the stack trace
    local displayed_stack=()
    local longest_length=0
    for ((i = 1; i < ${#FUNCNAME[@]}; i++)); do
        local func="${FUNCNAME[i]}"
        local line="${BASH_LINENO[i - 1]}"
        local current_length=${#func}

        if should_skip "$func"; then
            continue
        elif ((current_length > longest_length)); then
            longest_length=$current_length
        fi

        displayed_stack=("$(printf "%s|%s" "$func()" "$line")" "${displayed_stack[@]}")
    done

    # General text attributes
    local reset="\033[0m" # Reset text formatting
    local bold="\033[1m"  # Bold text

    # Foreground colors
    local fgred="\033[31m"       # Red text
    local fggrn="\033[32m"       # Green text
    local fgblu="\033[34m"       # Blue text
    local fgmag="\033[35m"       # Magenta text
    local fgcyn="\033[36m"       # Cyan text
    local fggld="\033[38;5;220m" # Gold text (ANSI 256 color)

    # Determine color and label based on level
    local color label
    case "$level" in
    DEBUG)
        color=$fgcyn
        label="[DEBUG]"
        ;;
    INFO)
        color=$fggrn
        label="[INFO ]"
        ;;
    WARN | WARNING)
        color=$fggld
        label="[WARN ]"
        ;;
    ERROR)
        color=$fgmag
        label="[ERROR]"
        ;;
    CRIT | CRITICAL)
        color=$fgred
        label="[CRIT ]"
        ;;
    esac

    # Create header and footer
    local dash_count=$(((width - ${#header_name} - 2) / 2))
    local header_l header_r
    header_l="$(printf '%*s' "$dash_count" '' | tr ' ' "$char")"
    header_r="$header_l"
    [[ $(((width - ${#header_name}) % 2)) -eq 1 ]] && header_r="${header_r}${char}"
    local header
    header=$(
        printf "%b%s%b %b%b%s%b %b%s%b" \
            "$color" \
            "$header_l" \
            "$reset" \
            "$color" \
            "$bold" \
            "$header_name" \
            "$reset" \
            "$color" \
            "$header_r" \
            "$reset"
    )

    # Construct the footer
    local footer line
    # Generate the line of characters
    line="$(printf '%*s' "$width" '' | tr ' ' "$char")"
    footer="$(printf '%b%s%b' "$color" "$line" "$reset")"

    # Print header
    printf "%s\n" "$header"

    # Print the message, if provided
    if [[ -n "$message" ]]; then
        # Fallback mechanism for wrap_messages
        local result primary overflow secondary
        if command -v wrap_messages >/dev/null 2>&1; then
            result=$(wrap_messages "$width" "$message" || true)
            primary="${result%%"${delimiter}"*}"
            result="${result#*"${delimiter}"}"
            overflow="${result%%"${delimiter}"*}"
        else
            primary="$message"
        fi
        # Print the formatted message
        printf "%b%s%b\n" "${color}" "${primary}" "${reset}"
        printf "%b%s%b\n" "${color}" "${overflow}" "${reset}"
    fi

    # Print stack trace
    local indent=$(((width / 2) - ((longest_length + 28) / 2)))
    indent=$((indent < 0 ? 0 : indent))
    if [[ -z "${displayed_stack[*]}" ]]; then
        printf "%b[WARN ]%b Stack trace is empty.\n" "$fggld" "$reset" >&2
    else
        for ((i = ${#displayed_stack[@]} - 1, idx = 0; i >= 0; i--, idx++)); do
            IFS='|' read -r func line <<<"${displayed_stack[i]}"
            printf "%b%*s [%d] Function: %-*s Line: %4s%b\n" \
                "$color" "$indent" ">" "$idx" "$((longest_length + 2))" "$func" "$line" "$reset"
        done
    fi

    # Print footer
    printf "%s\n\n" "$footer"
}

# -----------------------------------------------------------------------------
# @brief Logs a warning message with optional details and stack trace.
# @details This function logs a warning message with color-coded formatting
#          and optional details. It adjusts the output to fit within the
#          terminal's width and supports message wrapping if the
#          `wrap_messages` function is available. If `WARN_STACK_TRACE` is set
#          to `true`, a stack trace is also logged.
#
# @param $1 [Optional] The primary warning message. Defaults to
#                      "A warning was raised on this line" if not provided.
# @param $@ [Optional] Additional details to include in the warning message.
#
# @global FALLBACK_SCRIPT_NAME The name of the script to use if the script
#                              name cannot be determined.
# @global FUNCNAME             Bash array containing the function call stack.
# @global BASH_LINENO          Bash array containing the line numbers of
#                              function calls in the stack.
# @global WRAP_DELIMITER       The delimiter used for separating wrapped
#                              message parts.
# @global WARN_STACK_TRACE     If set to `true`, a stack trace will be logged.
# @global COLUMNS              The terminal's column width, used to format
#                              the output.
#
# @return None.
#
# @example
# warn "Configuration file missing." "Please check /etc/config."
# warn "Invalid syntax in the configuration file."
#
# @note This function requires `tput` for terminal width detection and ANSI
#       formatting, with fallbacks for minimal environments.
# -----------------------------------------------------------------------------
warn() {
    # Initialize variables
    local script="${FALLBACK_SCRIPT_NAME:-unknown}" # This script's name
    local func_name="${FUNCNAME[1]:-main}"          # Calling function
    local caller_line=${BASH_LINENO[0]:-0}          # Calling line

    # Get valid error code
    local error_code
    if [[ -n "${1:-}" && "$1" =~ ^[0-9]+$ ]]; then
        error_code=$((10#$1)) # Convert to numeric
        shift
    else
        error_code=1 # Default to 1 if not numeric
    fi

    # Configurable delimiter
    local delimiter="${WRAP_DELIMITER:-␞}"

    # Get the primary message
    local message
    message=$(sed -E 's/^[[:space:]]*//;s/[[:space:]]*$//' <<<"${1:-A warning was raised on this line}")
    [[ $# -gt 0 ]] && shift

    # Process details
    local details
    details=$(sed -E 's/^[[:space:]]*//;s/[[:space:]]*$//' <<<"$*")

    # Recalculate terminal columns
    COLUMNS=$( (command -v tput >/dev/null && tput cols) || printf "80")
    COLUMNS=$((COLUMNS > 0 ? COLUMNS : 80)) # Ensure COLUMNS is a positive number
    local width
    width=${COLUMNS:-80} # Max console width

    # Escape sequences for colors and attributes
    local reset="\033[0m"        # Reset text
    local bold="\033[1m"         # Bold text
    local fggld="\033[38;5;220m" # Gold text
    local fgcyn="\033[36m"       # Cyan text
    local fgblu="\033[34m"       # Blue text

    # Format prefix
    format_prefix() {
        local color=${1:-"\033[0m"}
        local label="${2:-'[WARN ] [unknown:main:0]'}"
        # Create prefix
        printf "%b%b%s%b %b[%s:%s:%s]%b " \
            "${bold}" \
            "${color}" \
            "${label}" \
            "${reset}" \
            "${bold}" \
            "${script}" \
            "${func_name}" \
            "${caller_line}" \
            "${reset}"
    }

    # Generate prefixes
    local warn_prefix extd_prefix dets_prefix
    warn_prefix=$(format_prefix "$fggld" "[WARN ]")
    extd_prefix=$(format_prefix "$fgcyn" "[EXTND]")
    dets_prefix=$(format_prefix "$fgblu" "[DETLS]")

    # Strip ANSI escape sequences for length calculation
    local plain_warn_prefix adjusted_width
    plain_warn_prefix=$(printf "%s" "$warn_prefix" | sed -E 's/(\x1b\[[0-9;]*[a-zA-Z]|\x1b\([a-zA-Z])//g; s/^[[:space:]]*//; s/[[:space:]]*$//')
    adjusted_width=$((width - ${#plain_warn_prefix} - 1))

    # Fallback mechanism for `wrap_messages`
    local result primary overflow secondary
    if command -v wrap_messages >/dev/null 2>&1; then
        result=$(wrap_messages "$adjusted_width" "$message" "$details" || true)
        primary="${result%%"${delimiter}"*}"
        result="${result#*"${delimiter}"}"
        overflow="${result%%"${delimiter}"*}"
        secondary="${result#*"${delimiter}"}"
    else
        primary="$message"
        overflow=""
        secondary="$details"
    fi

    # Print the primary message
    printf "%s%s\n" "$warn_prefix" "$primary" >&2

    # Print overflow lines
    if [[ -n "$overflow" ]]; then
        while IFS= read -r line; do
            printf "%s%s\n" "$extd_prefix" "$line" >&2
        done <<<"$overflow"
    fi

    # Print secondary details
    if [[ -n "$secondary" ]]; then
        while IFS= read -r line; do
            printf "%s%s\n" "$dets_prefix" "$line" >&2
        done <<<"$secondary"
    fi

    # Execute stack trace if WARN_STACK_TRACE is enabled
    if [[ "${WARN_STACK_TRACE:-false}" == "true" ]]; then
        stack_trace "WARNING" "${message}" "${secondary}"
    fi
}

# -----------------------------------------------------------------------------
# @brief Terminates the script with a critical error message.
# @details This function is used to log a critical error message with optional
#          details and exit the script with the specified error code. It
#          supports formatting the output with ANSI color codes, dynamic
#          column widths, and optional multi-line message wrapping.
#
#          If the optional `wrap_messages` function is available, it will be
#          used to wrap and combine messages. Otherwise, the function falls
#          back to printing the primary message and details as-is.
#
# @param $1 [optional] Numeric error code. Defaults to 1.
# @param $2 [optional] Primary error message. Defaults to "Critical error".
# @param $@ [optional] Additional details to include in the error message.
#
# @global FALLBACK_SCRIPT_NAME The script name to use as a fallback.
# @global FUNCNAME             Bash array containing the call stack.
# @global BASH_LINENO          Bash array containing line numbers of the stack.
# @global WRAP_DELIMITER       Delimiter used when combining wrapped messages.
# @global COLUMNS              The terminal's column width, used to adjust
#                              message formatting.
#
# @return None. This function does not return.
# @exit Exits the script with the specified error code.
#
# @example
# die 127 "File not found" "Please check the file path and try again."
# die "Critical configuration error"
# -----------------------------------------------------------------------------
die() {
    # Initialize variables
    local script="${FALLBACK_SCRIPT_NAME:-unknown}" # This script's name
    local func_name="${FUNCNAME[1]:-main}"          # Calling function
    local caller_line=${BASH_LINENO[0]:-0}          # Calling line

    # Get valid error code
    if [[ -n "${1:-}" && "$1" =~ ^[0-9]+$ ]]; then
        error_code=$((10#$1)) # Convert to numeric
        shift
    else
        error_code=1 # Default to 1 if not numeric
    fi

    # Configurable delimiter
    local delimiter="${WRAP_DELIMITER:-␞}"

    # Process the primary message
    local message
    message=$(sed -E 's/^[[:space:]]*//;s/[[:space:]]*$//' <<<"${1:-Critical error}")

    # Only shift if there are remaining arguments
    [[ $# -gt 0 ]] && shift

    # Process details
    local details
    details=$(sed -E 's/^[[:space:]]*//;s/[[:space:]]*$//' <<<"$*")

    # Recalculate terminal columns
    COLUMNS=$( (command -v tput >/dev/null && tput cols) || printf "80")
    COLUMNS=$((COLUMNS > 0 ? COLUMNS : 80)) # Ensure COLUMNS is a positive number
    local width
    width=${COLUMNS:-80} # Max console width

    # Escape sequences as safe(r) alternatives to global tput values
    # General attributes
    local reset="\033[0m"
    local bold="\033[1m"
    # Foreground colors
    local fgred="\033[31m" # Red text
    local fgcyn="\033[36m" # Cyan text
    local fgblu="\033[34m" # Blue text

    # Format prefix
    format_prefix() {
        local color=${1:-"\033[0m"}
        local label="${2:-'[CRIT ] [unknown:main:0]'}"
        # Create prefix
        printf "%b%b%s%b %b[%s:%s:%s]%b " \
            "${bold}" \
            "${color}" \
            "${label}" \
            "${reset}" \
            "${bold}" \
            "${script}" \
            "${func_name}" \
            "${caller_line}" \
            "${reset}"
    }

    # Generate prefixes
    local crit_prefix extd_prefix dets_prefix
    crit_prefix=$(format_prefix "$fgred" "[CRIT ]")
    extd_prefix=$(format_prefix "$fgcyn" "[EXTND]")
    dets_prefix=$(format_prefix "$fgblu" "[DETLS]")

    # Strip ANSI escape sequences for length calculation
    local plain_crit_prefix adjusted_width
    plain_crit_prefix=$(printf "%s" "$crit_prefix" | sed -E 's/(\x1b\[[0-9;]*[a-zA-Z]|\x1b\([a-zA-Z])//g; s/^[[:space:]]*//; s/[[:space:]]*$//')
    adjusted_width=$((width - ${#plain_crit_prefix} - 1))

    # Fallback mechanism for `wrap_messages` since it is external
    local result primary overflow secondary
    if command -v wrap_messages >/dev/null 2>&1; then
        result=$(wrap_messages "$adjusted_width" "$message" "$details" || true)
        primary="${result%%"${delimiter}"*}"
        result="${result#*"${delimiter}"}"
        overflow="${result%%"${delimiter}"*}"
        secondary="${result#*"${delimiter}"}"
    else
        primary="$message"
        overflow=""
        secondary="$details"
    fi

    # Print the primary message
    printf "%s%s\n" "$crit_prefix" "$primary" >&2

    # Print overflow lines
    if [[ -n "$overflow" ]]; then
        while IFS= read -r line; do
            printf "%s%s\n" "$extd_prefix" "$line" >&2
        done <<<"$overflow"
    fi

    # Print secondary details
    if [[ -n "$secondary" ]]; then
        while IFS= read -r line; do
            printf "%s%s\n" "$dets_prefix" "$line" >&2
        done <<<"$secondary"
    fi

    # Execute stack trace
    stack_trace "CRITICAL" "${message}" "${secondary}"

    # Exit with the specified error code
    exit "$error_code"
}

# -----------------------------------------------------------------------------
# @brief Add a dot (`.`) at the beginning of a string if it's missing.
# @details This function ensures the input string starts with a leading dot.
#          If the input string is empty, the function logs a warning and
#          returns an error code.
#
# @param $1 The input string to process.
#
# @return Outputs the modified string with a leading dot if it was missing.
# @retval 1 If the input string is empty.
#
# @example
# add_dot "example"   # Outputs ".example"
# add_dot ".example"  # Outputs ".example"
# add_dot ""          # Logs a warning and returns an error.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
add_dot() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local input=${1:-} # Input string to process

    # Validate input
    if [[ -z "$input" ]]; then
        warn "Input to add_dot cannot be empty."
        debug_end "$debug"
        return 1
    fi

    # Add a leading dot if it's missing
    if [[ "$input" != .* ]]; then
        input=".$input"
    fi

    debug_end "$debug"
    printf "%s\n" "$input"
}

# -----------------------------------------------------------------------------
# @brief Removes a leading dot from the input string, if present.
# @details This function checks if the input string starts with a dot (`.`)
#          and removes it. If the input is empty, an error message is logged.
#          The function handles empty strings by returning an error and logging
#          an appropriate warning message.
#
# @param $1 [required] The input string to process.
#
# @return 0 on success, 1 on failure (when the input is empty).
#
# @example
# remove_dot ".hidden"  # Output: "hidden"
# remove_dot "visible"  # Output: "visible"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
remove_dot() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local input=${1:-} # Input string to process

    # Validate input
    if [[ -z "$input" ]]; then
        warn "ERROR" "Input to remove_dot cannot be empty."
        debug_end "$debug"
        return 1
    fi

    # Remove the leading dot if present
    if [[ "$input" == *. ]]; then
        input="${input#.}"
    fi

    debug_end "$debug"
    printf "%s\n" "$input"
}

# -----------------------------------------------------------------------------
# @brief Adds a period to the end of the input string if it doesn't already
#        have one.
# @details This function checks if the input string has a trailing period.
#          If not, it appends one. If the input is empty, an error is logged.
#
# @param $1 [required] The input string to process.
#
# @return The input string with a period added at the end (if missing).
# @return 1 If the input string is empty.
#
# @example
# result=$(add_period "Hello")
# echo "$result"  # Output: "Hello."
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
add_period() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local input=${1:-} # Input string to process

    # Validate input
    if [[ -z "$input" ]]; then
        warn "Input to add_period cannot be empty."
        debug_end "$debug" # Next line must be a return/print/exit
        return 1
    fi

    # Add a trailing period if it's missing
    if [[ "$input" != *. ]]; then
        input="$input."
    fi

    debug_end "$debug"
    printf "%s\n" "$input"
}

# -----------------------------------------------------------------------------
# @brief Remove a trailing period (`.`) from a string if present.
# @details This function processes the input string and removes a trailing
#          period if it exists. If the input string is empty, the function logs
#          an error and returns an error code.
#
# @param $1 The input string to process.
#
# @return Outputs the modified string without a trailing period if one was
#         present.
# @retval 1 If the input string is empty.
#
# @example
# remove_period "example."  # Outputs "example"
# remove_period "example"   # Outputs "example"
# remove_period ""          # Logs an error and returns an error code.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
remove_period() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local input=${1:-} # Input string to process

    # Validate input
    if [[ -z "$input" ]]; then
        warn "ERROR" "Input to remove_period cannot be empty."
        debug_end "$debug"
        return 1
    fi

    # Remove the trailing period if present
    if [[ "$input" == *. ]]; then
        input="${input%.}"
    fi

    debug_end "$debug"
    printf "%s\n" "$input"
}

# -----------------------------------------------------------------------------
# @brief Add a trailing slash (`/`) to a string if it's missing.
# @details This function ensures that the input string ends with a trailing
#          slash. If the input string is empty, the function logs an error and
#          returns an error code.
#
# @param $1 The input string to process.
#
# @return Outputs the modified string with a trailing slash if one was missing.
# @retval 1 If the input string is empty.
#
# @example
# add_slash "/path/to/directory"  # Outputs "/path/to/directory/"
# add_slash "/path/to/directory/" # Outputs "/path/to/directory/"
# add_slash ""                    # Logs an error and returns an error code.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
add_slash() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local input="$1" # Input string to process

    # Validate input
    if [[ -z "${input:-}" ]]; then
        warn "ERROR" "Input to add_slash cannot be empty."
        debug_end "$debug"
        return 1
    fi

    # Add a trailing slash if it's missing
    if [[ "$input" != */ ]]; then
        input="$input/"
    fi

    debug_end "$debug"
    printf "%s\n" "$input"
}

# -----------------------------------------------------------------------------
# @brief Remove a trailing slash (`/`) from a string if present.
# @details This function ensures that the input string does not end with a
#          trailing slash. If the input string is empty, the function logs an
#          error and returns an error code.
#
# @param $1 The input string to process.
#
# @return Outputs the modified string without a trailing slash if one was
#         present.
# @retval 1 If the input string is empty.
#
# @example
# remove_slash "/path/to/directory/"  # Outputs "/path/to/directory"
# remove_slash "/path/to/directory"   # Outputs "/path/to/directory"
# remove_slash ""                     # Logs an error and returns an error code
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
remove_slash() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local input="$1" # Input string to process

    # Validate input
    if [[ -z "${input:-}" ]]; then
        warn "ERROR" "Input to remove_slash cannot be empty."
        debug_end "$debug"
        return 1
    fi

    # Remove the trailing slash if present
    if [[ "$input" == */ ]]; then
        input="${input%/}"
    fi

    debug_end "$debug"
    printf "%s\n" "$input"
}

# -----------------------------------------------------------------------------
# @brief Render command arguments with unambiguous Bash-safe quoting.
# @param ... Command argv to render.
# @return A single display-only string; no argument is evaluated.
# -----------------------------------------------------------------------------
render_command_argv() {
    local arg quoted rendered=""
    for arg in "$@"; do
        printf -v quoted '%q' "$arg"
        rendered+="${rendered:+ }${quoted}"
    done
    printf '%s' "$rendered"
}

# -----------------------------------------------------------------------------
# @brief Executes a command in a separate Bash process.
# @details This function manages the execution of a shell command, handling the
#          display of status messages. Child stdout and stderr are always
#          suppressed; debug mode renders the safely quoted command through the
#          wrapper's own diagnostics. It supports dry-run mode, where the
#          command is simulated without execution. The function prints success
#          or failure messages and removes the "Running" line once the command
#          finishes.
#
# @param exec_name The name of the command or task being executed.
# @param ...       The command and its arguments, followed optionally by the
#                  debug flag as the last parameter.
#
# @return Returns 0 if the command was successful, non-zero otherwise.
#
# @note The function supports dry-run mode, controlled by the DRY_RUN variable.
#       When DRY_RUN is true, the command is only simulated without actual
#       execution.
#
# @example
# exec_command "Test Command" echo Hello World debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
exec_command() {
    # The documented debug control is a trailing wrapper argument. Preserve
    # command arguments named "debug" anywhere else with exact argv fidelity.
    local debug="" status
    if (( $# > 0 )) && [[ "${!#}" == "debug" ]]; then
        debug=$(debug_start "debug")
        set -- "${@:1:$(($# - 1))}"
    else
        debug=$(debug_start)
    fi

    # Declare local variables after debug initialization
    local status=0 exec_name running_pre complete_pre failed_pre
    local args cmd cmd_str failure_output_file output_line
    local -a pipeline_status=()
    local status_mode="${EXEC_COMMAND_STATUS_MODE:-execution}"
    failure_output_file="${EXEC_COMMAND_FAILURE_OUTPUT_FILE:-}"

    # Assign the human readable name and shift it off
    exec_name="$1"; shift

    # Collect command arguments (debug has already been filtered out)
    local args=( "$@" )

    # Now also remove any empty trailing arguments to be safe
    while (( ${#args[@]} > 0 )) && [[ -z "${args[-1]}" ]]; do
        unset 'args[-1]'
    done

    # Build the actual command array
    cmd=( "${args[@]}" )

    # Render argv for display without losing argument boundaries or allowing
    # shell metacharacters to look executable.
    cmd_str=$(render_command_argv "${cmd[@]}")
    if [[ "$debug" == "debug" ]]; then
        debug_print "Name:    $exec_name" "$debug"
        debug_print "Command: $cmd_str" "$debug"
    fi

    case "$status_mode" in
        execution|info) ;;
        *)
            warn "Unsupported exec_command status mode: $status_mode"
            debug_end "$debug"
            return 2
            ;;
    esac

    if [[ -n "$failure_output_file" ]]; then
        if [[ -e "$failure_output_file" || -L "$failure_output_file" ||
              ! -d "${failure_output_file%/*}" || -L "${failure_output_file%/*}" ]]; then
            warn "Refusing unsafe command failure-output path: $failure_output_file"
            debug_end "$debug"
            return 2
        fi
        if ! (umask 077; set -o noclobber; : >"$failure_output_file") 2>/dev/null; then
            warn "Unable to create command failure-output file: $failure_output_file"
            debug_end "$debug"
            return 2
        fi
        if [[ ! -f "$failure_output_file" || -L "$failure_output_file" ]]; then
            warn "Command failure-output file is not a regular file: $failure_output_file"
            debug_end "$debug"
            return 2
        fi
    fi

    # Read-only planning is useful operator context, not a completed mutation.
    # Keep it inside this wrapper so dry runs still suppress command execution
    # and debug mode still renders the exact argv.
    if [[ "$status_mode" == "info" ]]; then
        logI "$exec_name."
        if [[ "$DRY_RUN" == "true" ]]; then
            debug_end "$debug"
            return 0
        fi

        if [[ -n "$failure_output_file" ]]; then
            if "${cmd[@]}" 2>&1 | tail -c 16384 >"$failure_output_file"; then
                pipeline_status=("${PIPESTATUS[@]}")
            else
                pipeline_status=("${PIPESTATUS[@]}")
            fi
            status="${pipeline_status[0]}"
            if [[ "$status" -eq 0 && "${pipeline_status[1]}" -ne 0 ]]; then
                status="${pipeline_status[1]}"
            fi
        else
            "${cmd[@]}" &>/dev/null || status=$?
        fi
        if [[ $status -ne 0 ]]; then
            logE "Failed: $exec_name."
            if [[ $status -eq 127 ]]; then
                warn "Command not found: ${cmd_str}"
            else
                warn "Command failed with status $status: ${cmd_str}"
            fi
            if [[ -n "$failure_output_file" && -s "$failure_output_file" ]]; then
                logW "Failure details from $exec_name (last 20 lines, 16 KiB maximum):"
                while IFS= read -r output_line || [[ -n "$output_line" ]]; do
                    logW "${output_line:0:500}"
                done < <(tail -c 16384 -- "$failure_output_file" |
                    tail -n 20 | LC_ALL=C tr -cd '\11\12\40-\176')
            fi
        fi
        debug_end "$debug"
        return "$status"
    fi

    # Prepare status prefixes
    running_pre="Running:" complete_pre="Complete:" failed_pre="Failed:"
    if [[ "$DRY_RUN" == "true" ]]; then
        running_pre+=" (dry)"
        complete_pre+=" (dry)"
        failed_pre+=" (dry)"
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        printf "%b[  -  ]%b %s %s.\n" "${FGGLD}" "${RESET}" "$running_pre" "$exec_name"
        sleep 0.02
        printf "%b%b" "$MOVE_UP" "$CLEAR_LINE"
        printf "%b[✔]%b %s %s.\n" "${FGGRN}" "${RESET}" "$complete_pre" "$exec_name"
        debug_end "$debug"
        return 0
    fi

    # Show running status line
    printf "%b[  -  ]%b %s %s.\n" "${FGGLD}" "${RESET}" "$running_pre" "$exec_name"
    sleep 0.02

    # Child output never bypasses the wrapper. Debug mode has already rendered
    # the exact command through debug_print().
    if [[ -n "$failure_output_file" ]]; then
        if "${cmd[@]}" 2>&1 | tail -c 16384 >"$failure_output_file"; then
            pipeline_status=("${PIPESTATUS[@]}")
        else
            pipeline_status=("${PIPESTATUS[@]}")
        fi
        status="${pipeline_status[0]}"
        if [[ "$status" -eq 0 && "${pipeline_status[1]}" -ne 0 ]]; then
            status="${pipeline_status[1]}"
        fi
    else
        "${cmd[@]}" &>/dev/null || status=$?
    fi

    printf "%b%b" "$MOVE_UP" "$CLEAR_LINE"

    # Report result
    if [[ $status -eq 0 ]]; then
        printf "%b[  ✔  ]%b %s %s.\n" "${FGGRN}" "${RESET}" "$complete_pre" "$exec_name"
    else
        printf "%b[  ✘  ]%b %s %s.\n" "${FGRED}" "${RESET}" "$failed_pre" "$exec_name"
        if [[ $status -eq 127 ]]; then
            warn "Command not found: ${cmd_str}"
        else
            warn "Command failed with status $status: ${cmd_str}"
        fi
        if [[ -n "$failure_output_file" && -s "$failure_output_file" ]]; then
            logW "Failure details from $exec_name (last 20 lines, 16 KiB maximum):"
            while IFS= read -r output_line || [[ -n "$output_line" ]]; do
                logW "${output_line:0:500}"
            done < <(tail -c 16384 -- "$failure_output_file" |
                tail -n 20 | LC_ALL=C tr -cd '\11\12\40-\176')
        fi
    fi

    debug_end "$debug"
    return "$status"
}

# -----------------------------------------------------------------------------
# @brief  Comment or uncomment lines ending with a given suffix.
# @details
#   Searches for lines in a file whose text ends with the given
#   suffix, then either comments them (adds "# ") or
#   uncomments them (removes "#" plus any following space).
#   Honors $DRY_RUN: when true, only logs what would run.
#
# @param  $1  Filename to process.
# @param  $2  Suffix to match at end of lines (literal, not a regex).
# @param  $3  Action: "comment" to add "# ", "uncomment" to remove it.
#
# @global DRY_RUN  If "true", commands are only logged.
#
# @return 0 on success, non-zero on failure.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
modify_comment_lines() {
    # Start debug and filter parameters
    local debug status
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local file="$1"; shift
    local suffix="$1"; shift
    local action="$1"; shift

    # Sanity checks
    if [[ -z "$file" || -z "$suffix" || -z "$action" ]]; then
        logE "Usage: modify_comment_lines <file> <suffix> <comment|uncomment>"
        debug_end "$debug"; return 1
    fi

    if [[ "$debug" == "debug" ]]; then
        logD "File: ${file}"
        logD "File: ${suffix}"
        logD "File: ${action}"
    fi

    if [[ ! -f "$file" ]]; then
        logE "File not found: $file"
        debug_end "$debug"; return 1
    fi
    if [[ "$action" != "comment" && "$action" != "uncomment" ]]; then
        logE "Invalid action '$action'; use 'comment' or 'uncomment'"
        debug_end "$debug"; return 1
    fi

    # Escape any "/" in the suffix for our sed script
    local esc_suffix=${suffix//\//\\/}

    # Build the sed command
    local sed_script
    if [[ "$action" == "comment" ]]; then
        sed_script="/${esc_suffix}\$/ { /^[[:space:]]*#/! s|^|# | }"
    else
        sed_script="/${esc_suffix}\$/ { s|^[[:space:]]*#\s*|| }"
    fi

    debug_print "Prepared sed script: $sed_script" "$debug"

    if [[ "$DRY_RUN" == "true" ]]; then
        # Just show what would happen
        logD "Exec: sed -i '$sed_script' '$file'"
    else
        # Actually apply it
        exec_command "Update log directives in $file" sed -i "$sed_script" "$file" "$debug" || {
            logE "sed failed on $file"
            debug_end "$debug"; return 1
        }
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Replace a placeholder string in a file.
# @details This function searches for a given string within a file, where the
#          string is bordered by "%" characters (e.g., "%SEARCH_STRING%"). If
#          found, it replaces the entire placeholder (including "%" signs) with
#          the specified replacement string.
#
# @param $1 Path to the file to modify.
# @param $2 Search string (without % signs).
# @param $3 Replacement string.
#
# @global None.
#
# @throws Exits with an error if the file does not exist or cannot be modified.
#
# @return 0 on success, non-zero on failure.
#
# @example
# replace_string_in_script "script.sh" "PLACEHOLDER" "new_value"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
replace_string_in_script() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local file_path="$1"
    local search_string="$2"
    local replace_string="$3"

    if [[ "$DRY_RUN" == "true" ]]; then
        local planned_command
        planned_command=$(render_command_argv \
            sed -i "s|%${search_string}%|${replace_string}|g" "$file_path")
        debug_print "Dry run command: $planned_command" "$debug"
        debug_end "$debug"
        return 0
    fi

    if [[ ! -f "$file_path" ]]; then
        debug_print "Error: File '$file_path' not found." "$debug"
        debug_end "$debug"
        return 1
    fi

    local pattern="%${search_string}%"

    # Use sed to replace all occurrences of %SEARCH_STRING% with REPLACE_STRING.
    # Propagate failure so callers staging privileged files cannot continue with
    # an incomplete render.
    if ! exec_command "Set $search_string in $file_path" \
        sed -i "s|${pattern}|${replace_string}|g" "$file_path" "$debug"; then
        logE "Failed to replace '${pattern}' in '$file_path'."
        debug_end "$debug"
        return 1
    fi

    debug_print "Replaced occurrences of '${pattern}' with '${replace_string}' in '$file_path'." "$debug"

    debug_end "$debug"
}

# -----------------------------------------------------------------------------
# @fn pause
# @brief Pauses execution until a single key is pressed.
# @details Displays the prompt “Press any key to continue…” and waits for
#          exactly one keystroke (no Enter required). It reads directly
#          from /dev/tty so it still works when the script’s stdin is
#          coming from a pipe (e.g. `curl … | bash`).
#
# @example
# pause
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
pause() {
    # Prompt the user
    printf "Press any key to continue..."
    # Read one character silently from the controlling terminal
    read -n1 -s -r -p "" </dev/tty || true
    # Newline after keypress
    printf "\n"
    return 0
}

############
### Print/Display Environment Functions
############

# -----------------------------------------------------------------------------
# @brief Retrieve the full name of the detected Raspberry Pi model.
# @details This function reads the Raspberry Pi compatibility string from
#          /proc/device-tree/compatible, extracts the model identifier, and
#          matches it against the SUPPORTED_MODELS associative array. When a
#          matching entry is found, the full descriptive model name is printed
#          to standard output.
#
#          The function uses the standard debug_start and debug_filter helpers
#          for argument handling. If debug output is enabled, the detected
#          model identifier is printed using debug_print.
#
#          If the model cannot be detected or is not present in
#          SUPPORTED_MODELS, the function terminates execution using die with
#          a non-zero exit status.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global SUPPORTED_MODELS Associative array mapping model identifiers to
#          support status entries using the format
#          "Full Name|model|chip".
#
# @return None. The full model name is written to standard output.
#
# @example
# model_name=$(print_model_name debug)
# -----------------------------------------------------------------------------
print_model_name() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local detected_model key full_name model chip

    # Read and process the compatible string
    if ! detected_model=$(tr '\0' '\n' </proc/device-tree/compatible 2>/dev/null \
        | sed -n 's/raspberrypi,//p'); then
        if [[ "$INSTALL_RP1_GPCLK_DKMS" == "true" ]]; then
            warn "Platform model detection is unavailable; continuing because RP1 provider installation was explicitly requested."
            debug_end "$debug"
            return 0
        fi
        debug_end "$debug"
        die 1 "Failed to read or process /proc/device-tree/compatible."
    fi

    # Ensure a model was detected
    if [[ -z "${detected_model:-}" ]]; then
        if [[ "$INSTALL_RP1_GPCLK_DKMS" == "true" ]]; then
            warn "This system is not identified as a Raspberry Pi; continuing because RP1 provider installation was explicitly requested."
            debug_end "$debug"
            return 0
        fi
        debug_end "$debug"
        die 1 "No Raspberry Pi model found in /proc/device-tree/compatible."
    fi

    # Match detected model against SUPPORTED_MODELS keys
    for key in "${!SUPPORTED_MODELS[@]}"; do
        IFS='|' read -r full_name model chip <<<"$key"
        if [[ "$model" == "$detected_model" ]]; then
            logI "Detected model: $full_name." "$debug"
            debug_end "$debug"
            return 0
        fi
    done

    if [[ "$INSTALL_RP1_GPCLK_DKMS" == "true" ]]; then
        warn "Detected Raspberry Pi model '$detected_model' is not recognized; continuing because RP1 provider installation was explicitly requested."
        debug_end "$debug"
        return 0
    fi

    debug_end "$debug"
    die 1 "Detected Raspberry Pi model '$detected_model' is not recognized."
}

is_pi5() {
    local detected_model

    if ! detected_model=$(tr '\0' '\n' </proc/device-tree/compatible 2>/dev/null \
        | sed -n 's/raspberrypi,//p'); then
        return 1
    fi

    [[ "${detected_model:-}" == "5-model-b" || \
        "${detected_model:-}" == "5-compute-module" ]]
}

is_rp1_system() {
    local compatible_file="${1:-/proc/device-tree/compatible}"
    local compatible

    compatible=$(tr '\0' '\n' <"$compatible_file" 2>/dev/null) || return 1
    grep -Fqx -e 'raspberrypi,5-model-b' -e 'raspberrypi,5-compute-module' \
        <<<"$compatible" || return 1
    grep -Fqx 'brcm,bcm2712' <<<"$compatible"
}

# -----------------------------------------------------------------------------
# @brief Return success only when RP1 provider work belongs in this install.
# @details Automatic selection is limited to positively identified Pi 5/CM5
#          systems. Explicit true remains an operator override so the provider
#          helper can validate and fail closed; explicit false skips all RP1
#          planning and lifecycle output on every platform. Unknown values are
#          passed through to the helper for its existing validation error.
# -----------------------------------------------------------------------------
rp1_gpclk_dkms_installation_selected() {
    case "${INSTALL_RP1_GPCLK_DKMS:-auto}" in
        false)
            return 1
            ;;
        auto)
            is_rp1_system
            return "$?"
            ;;
        *)
            return 0
            ;;
    esac
}

# -----------------------------------------------------------------------------
# @brief Resolve the APT package closure for this installer invocation.
# @details Base application build packages apply on every supported Pi. RP1
#          DKMS, device-tree and kernel-module tools plus exact running-kernel
#          headers are appended only when RP1 provider installation is selected.
# @param $1 Optional resolved libgpiod runtime package.
# -----------------------------------------------------------------------------
resolve_apt_package_list() {
    local runtime_pkg="${1:-}"
    local kernel_release=""

    RESOLVED_APT_PACKAGES=( "${RUNTIME_APT_PACKAGES[@]}" )
    if [[ "${NO_WEB:-false}" != "true" ]]; then
        RESOLVED_APT_PACKAGES+=( "${WEB_APT_PACKAGES[@]}" )
    fi
    if [[ "$BINARY_SOURCE" == "build" ]]; then
        RESOLVED_APT_PACKAGES+=( "${BUILD_APT_PACKAGES[@]}" )
    else
        RESOLVED_APT_PACKAGES+=( "${BINARY_VALIDATION_APT_PACKAGES[@]}" "${BINARY_RUNTIME_PACKAGES[@]}" )
    fi
    if [[ -n "$runtime_pkg" ]]; then
        RESOLVED_APT_PACKAGES+=( "$runtime_pkg" )
    fi
    if ! rp1_gpclk_dkms_installation_selected; then
        return 0
    fi

    kernel_release=$(uname -r) || {
        warn "Unable to identify the running kernel for RP1 DKMS headers."
        return 1
    }
    if [[ ! "$kernel_release" =~ ^[0-9A-Za-z][0-9A-Za-z.+:~_-]*$ ]]; then
        warn "The running kernel release cannot form a safe APT package name: $kernel_release"
        return 1
    fi
    RESOLVED_APT_PACKAGES+=(
        "${RP1_GPCLK_DKMS_APT_PACKAGES[@]}"
        "linux-headers-${kernel_release}"
    )
}

# -----------------------------------------------------------------------------
# @brief Verify the exact RP1 DKMS build closure after package handling.
# @details This check is conditional on RP1 installation, is observational only,
#          and fails closed before provider apply. Optional path arguments exist
#          solely so hardware-free tests can provide a synthetic header layout.
# -----------------------------------------------------------------------------
validate_rp1_gpclk_build_dependencies() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    [[ "$ACTION" == "install" ]] || return 0
    rp1_gpclk_dkms_installation_selected || return 0
    if [[ "$DRY_RUN" == "true" ]]; then
        debug_print "Dry run: RP1 DKMS build dependency validation was not executed." "$debug"
        debug_end "$debug"
        return 0
    fi

    local kernel_release="${1:-}"
    local modules_base="${2:-/lib/modules}"
    local headers_base="${3:-/usr/src}"
    local header_package=""
    local expected_headers=""
    local build_link=""
    local expected_real=""
    local build_real=""
    local tool=""
    local missing=0

    if [[ -z "$kernel_release" ]]; then
        kernel_release=$(uname -r) || {
            warn "Unable to identify the running kernel for RP1 DKMS validation."
            return 1
        }
    fi
    if [[ ! "$kernel_release" =~ ^[0-9A-Za-z][0-9A-Za-z.+:~_-]*$ ]]; then
        warn "Invalid running-kernel release for RP1 DKMS validation: $kernel_release"
        return 1
    fi

    header_package="linux-headers-${kernel_release}"
    expected_headers="${headers_base}/${header_package}"
    build_link="${modules_base}/${kernel_release}/build"
    if ! dpkg-query -W -f='${Status}' "$header_package" 2>/dev/null |
        grep -q "install ok installed"; then
        warn "Exact RP1 DKMS header package is not installed: $header_package"
        ((missing++))
    fi
    expected_real=$(realpath "$expected_headers" 2>/dev/null) || expected_real=""
    build_real=$(realpath "$build_link" 2>/dev/null) || build_real=""
    if [[ -z "$expected_real" || -z "$build_real" ||
        "$expected_real" != "$build_real" || ! -f "$build_real/Makefile" ]]; then
        warn "RP1 DKMS headers do not resolve to the exact running-kernel tree: $build_link -> $expected_headers"
        ((missing++))
    fi

    for tool in make dkms depmod modinfo sha256sum dtc fdtput; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            warn "Missing RP1 DKMS build dependency: $tool"
            ((missing++))
        fi
    done
    if ! command -v cc >/dev/null 2>&1 && ! command -v gcc >/dev/null 2>&1; then
        warn "Missing RP1 DKMS compiler: expected cc or gcc."
        ((missing++))
    fi
    if ((missing > 0)); then
        warn "RP1 DKMS build dependency validation failed with $missing issue(s)."
        debug_end "$debug"
        return 1
    fi

    debug_print "RP1 DKMS build dependencies match kernel $kernel_release." "$debug"
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Remove only the fresh RP1 provider planning directory from this run.
# @details The independently owned installed provider and its operational record
#          are deliberately preserved. This cleanup owns only mktemp output.
# -----------------------------------------------------------------------------
cleanup_rp1_gpclk_dkms_state() {
    local state_dir="${RP1_GPCLK_DKMS_STATE_DIR:-}"
    if [[ "$state_dir" == "dry-run:no-state-created" ]]; then
        RP1_GPCLK_DKMS_STATE_DIR=""
        RP1_GPCLK_DKMS_HELPER=""
        return 0
    fi
    if [[ -z "$state_dir" || ! -d "$state_dir" || -L "$state_dir" ]]; then
        return 0
    fi
    case "$state_dir" in
        /tmp/wsprrypi-rp1-gpclk-dkms.*|/var/tmp/wsprrypi-rp1-gpclk-dkms.*)
            rm -rf -- "$state_dir"
            ;;
        *)
            warn "Refusing to remove unexpected RP1 provider state directory: $state_dir"
            return 1
            ;;
    esac
    RP1_GPCLK_DKMS_STATE_DIR=""
    RP1_GPCLK_DKMS_HELPER=""
    return 0
}

# -----------------------------------------------------------------------------
# @brief Resolve and validate the RP1-GPCLK-DKMS installation before mutation.
# @details Uses the checked-out helper when available. A streamed installer
#          retrieves the helper from the exact WsprryPi source selector already
#          chosen for this installer invocation. The helper enumerates and
#          validates release metadata or resolves an immutable development
#          source before any package-manager operation.
# -----------------------------------------------------------------------------
prepare_rp1_gpclk_dkms_installation() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    [[ "$ACTION" == "install" ]] || return 0
    rp1_gpclk_dkms_installation_selected || return 0

    local script_source="${BASH_SOURCE[0]:-}"
    local local_helper=""
    if [[ -n "$script_source" && "$script_source" != "bash" ]]; then
        local_helper="$(cd "$(dirname "$script_source")" && pwd -P)/rp1_gpclk_dkms_install.py"
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        # Use stable display-only paths. exec_command() deliberately does not
        # execute Python in dry-run mode, so no helper download, temporary
        # directory, network resolution, package inspection, or preflight runs.
        RP1_GPCLK_DKMS_STATE_DIR="dry-run:no-state-created"
        RP1_GPCLK_DKMS_HELPER="${local_helper:-dry-run:no-helper-downloaded}"
    else
        RP1_GPCLK_DKMS_STATE_DIR=$(mktemp -d /tmp/wsprrypi-rp1-gpclk-dkms.XXXXXX)
        exec_command "Protect RP1 helper staging directory" chmod 0700 "$RP1_GPCLK_DKMS_STATE_DIR" "$debug" || return 1
        RP1_GPCLK_DKMS_HELPER="$RP1_GPCLK_DKMS_STATE_DIR/rp1_gpclk_dkms_install.py"
        if [[ -n "$local_helper" && -f "$local_helper" && ! -L "$local_helper" ]]; then
            exec_command "Stage RP1 installer helper" cp -- "$local_helper" "$RP1_GPCLK_DKMS_HELPER" "$debug" || return 1
            exec_command "Protect RP1 installer helper" chmod 0700 "$RP1_GPCLK_DKMS_HELPER" "$debug" || return 1
        else
            local helper_url="${GIT_RAW_BASE}/${REPO_ORG}/${REPO_NAME}/${REPO_BRANCH}/scripts/rp1_gpclk_dkms_install.py"
            if ! exec_command "Download RP1 installer helper" curl --proto '=https' --tlsv1.2 --fail --silent --show-error \
                --output "$RP1_GPCLK_DKMS_HELPER" "$helper_url" "$debug"; then
                warn "Unable to retrieve the RP1-GPCLK-DKMS installer helper from $helper_url."
                cleanup_rp1_gpclk_dkms_state
                return 1
            fi
            exec_command "Protect RP1 installer helper" chmod 0700 "$RP1_GPCLK_DKMS_HELPER" "$debug" || return 1
        fi
    fi

    local prepare_args=(
        prepare
        --state-dir "$RP1_GPCLK_DKMS_STATE_DIR"
        --install "$INSTALL_RP1_GPCLK_DKMS"
        --source "$RP1_GPCLK_DKMS_SOURCE"
        --wsprry-source "$REPO_BRANCH"
    )
    if [[ "$DRY_RUN" == "true" ]]; then
        prepare_args+=(--dry-run)
    fi
    if [[ "$debug" == "debug" ]]; then
        prepare_args+=(--debug)
    fi

    if ! EXEC_COMMAND_STATUS_MODE=info \
        exec_command "Resolve RP1-GPCLK-DKMS installation plan" \
        python3 "$RP1_GPCLK_DKMS_HELPER" "${prepare_args[@]}" "$debug"; then
        warn "RP1-GPCLK-DKMS installation planning failed before package mutation."
        cleanup_rp1_gpclk_dkms_state
        return 1
    fi
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Apply the previously validated RP1 provider plan.
# @details Dry runs stop after rendering the plan. Real installations revalidate
#          prepared inputs and invoke only the package manager or the reviewed
#          upstream exact-source lifecycle. No overlay, route, load, service,
#          GPIO, output, transmission, or RF operation is performed.
# -----------------------------------------------------------------------------
apply_rp1_gpclk_dkms_installation() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    [[ "$ACTION" == "install" ]] || return 0
    rp1_gpclk_dkms_installation_selected || return 0
    if [[ "$DRY_RUN" != "true" && \
        ( -z "${RP1_GPCLK_DKMS_STATE_DIR:-}" || -z "${RP1_GPCLK_DKMS_HELPER:-}" ) ]]; then
        warn "RP1-GPCLK-DKMS plan state is unavailable."
        return 1
    fi

    local apply_args=(apply --state-dir "$RP1_GPCLK_DKMS_STATE_DIR")
    local failure_output_file=""
    if [[ "$DRY_RUN" != "true" ]]; then
        failure_output_file="$RP1_GPCLK_DKMS_STATE_DIR/apply-output.log"
    fi
    if [[ "$debug" == "debug" ]]; then
        apply_args+=(--debug)
    fi
    if ! EXEC_COMMAND_FAILURE_OUTPUT_FILE="$failure_output_file" \
        exec_command "Apply RP1-GPCLK-DKMS installation plan" \
        python3 "$RP1_GPCLK_DKMS_HELPER" "${apply_args[@]}" "$debug"; then
        warn "RP1-GPCLK-DKMS installation failed closed; inspect the failure details above, retained installer log, and provider state."
        return 1
    fi
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Recover owned RP1 administration before package/provider mutation.
# @details A repeat installation must not invalidate a completed activation
#          journal by replacing or restarting WsprryPi first. The helper
#          performs only the provider's digest-reviewed recovery transaction;
#          fresh/provider-only installs and dry runs remain no-ops.
# -----------------------------------------------------------------------------
prepare_rp1_gpclk_runtime_update() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    [[ "$ACTION" == "install" ]] || return 0
    rp1_gpclk_dkms_installation_selected || return 0
    if [[ "$DRY_RUN" != "true" &&
        ( -z "${RP1_GPCLK_DKMS_STATE_DIR:-}" || -z "${RP1_GPCLK_DKMS_HELPER:-}" ) ]]; then
        warn "RP1-GPCLK-DKMS plan state is unavailable."
        return 1
    fi

    local update_args=(prepare-runtime-update --state-dir "$RP1_GPCLK_DKMS_STATE_DIR")
    local failure_output_file=""
    if [[ "$DRY_RUN" != "true" ]]; then
        failure_output_file="$RP1_GPCLK_DKMS_STATE_DIR/runtime-update-output.log"
    fi
    if [[ "$debug" == "debug" ]]; then
        update_args+=(--debug)
    fi
    if ! EXEC_COMMAND_FAILURE_OUTPUT_FILE="$failure_output_file" \
        exec_command "Prepare neutral RP1 runtime for application update" \
        python3 "$RP1_GPCLK_DKMS_HELPER" "${update_args[@]}" "$debug"; then
        warn "RP1-GPCLK-DKMS runtime update preparation failed closed; application files and services were not changed."
        return 1
    fi
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Establish digest-bound, route-neutral RP1 runtime administration.
# @details Runs only after the exact WsprryPi route companion is installed.
#          The helper builds and reviews the exact runtime bundle, executes the
#          reviewed deployment and neutral-activation plans, and requires
#          neutral_ready with administration eligible and transmission
#          ineligible before restoring an explicit saved RP1 route. Neither
#          stage authorizes transmission.
# -----------------------------------------------------------------------------
activate_rp1_gpclk_runtime_administration() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    [[ "$ACTION" == "install" ]] || return 0
    rp1_gpclk_dkms_installation_selected || return 0
    if [[ "$DRY_RUN" != "true" && \
        ( -z "${RP1_GPCLK_DKMS_STATE_DIR:-}" || -z "${RP1_GPCLK_DKMS_HELPER:-}" ) ]]; then
        warn "RP1-GPCLK-DKMS runtime plan state is unavailable."
        return 1
    fi

    local runtime_args=(activate-runtime --state-dir "$RP1_GPCLK_DKMS_STATE_DIR")
    local failure_output_file=""
    if [[ "$DRY_RUN" != "true" ]]; then
        failure_output_file="$RP1_GPCLK_DKMS_STATE_DIR/activation-output.log"
    fi
    if [[ "$debug" == "debug" ]]; then
        runtime_args+=(--debug)
    fi
    if ! EXEC_COMMAND_FAILURE_OUTPUT_FILE="$failure_output_file" \
        exec_command "Activate neutral RP1-GPCLK-DKMS administration" \
        python3 "$RP1_GPCLK_DKMS_HELPER" "${runtime_args[@]}" "$debug"; then
        warn "RP1-GPCLK-DKMS neutral runtime activation failed closed; no GPIO route or output was requested."
        return 1
    fi
    exec_command "Reload systemd after RP1 runtime activation" \
        systemctl daemon-reload "$debug" || return 1
    exec_command "Start WsprryPi after RP1 runtime activation" \
        systemctl start "$WSPR_SERVICE.service" "$debug" || return 1
    # Neutral administration is only the provider bootstrap. Explicit persisted
    # RP1 configuration must also regain its route before setup reports success.
    if [[ "$DRY_RUN" != "true" ]]; then
        failure_output_file="$RP1_GPCLK_DKMS_STATE_DIR/route-restoration-output.log"
    fi
    if ! EXEC_COMMAND_FAILURE_OUTPUT_FILE="$failure_output_file" \
        exec_command "Restore configured RP1 route with transmission disabled" \
        python3 /usr/local/lib/wsprrypi/runtime_reconcile.py install "$debug"; then
        warn "Configured RP1 route restoration failed; setup cannot confirm transmitter readiness. Inspect the retained installer log and provider state."
        return 1
    fi
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Recover an exact owned neutral runtime before application teardown.
# @details A v3 runtime activation records the running application instance.
#          Recover it while that instance still exists so later service and
#          file removal cannot turn terminal evidence into a PID-drift conflict.
# -----------------------------------------------------------------------------
prepare_owned_rp1_gpclk_runtime_removal() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    [[ "$ACTION" == "uninstall" ]] || return 0
    [[ "$REMOVE_RP1_GPCLK_DKMS" != "false" ]] || return 0

    local script_source="${BASH_SOURCE[0]:-}"
    local local_helper=""
    local state_dir=""
    if [[ -n "$script_source" && "$script_source" != "bash" ]]; then
        local_helper="$(cd "$(dirname "$script_source")" && pwd -P)/rp1_gpclk_dkms_install.py"
    fi
    if [[ "$DRY_RUN" == "true" ]]; then
        logI "Dry run: would recover exact owned RP1 runtime before application teardown."
        debug_end "$debug"
        return 0
    fi
    state_dir=$(mktemp -d /tmp/wsprrypi-rp1-gpclk-dkms.XXXXXX)
    exec_command "Protect RP1 helper staging directory" chmod 0700 "$state_dir" "$debug" || return 1
    local helper="$state_dir/rp1_gpclk_dkms_install.py"
    if [[ -n "$local_helper" && -f "$local_helper" && ! -L "$local_helper" ]]; then
        exec_command "Stage RP1 installer helper" cp -- "$local_helper" "$helper" "$debug" || return 1
        exec_command "Protect RP1 installer helper" chmod 0700 "$helper" "$debug" || return 1
    else
        local helper_url="${GIT_RAW_BASE}/${REPO_ORG}/${REPO_NAME}/${REPO_BRANCH}/scripts/rp1_gpclk_dkms_install.py"
        if ! exec_command "Download RP1 installer helper" curl --proto '=https' --tlsv1.2 --fail --silent --show-error \
            --output "$helper" "$helper_url" "$debug"; then
            warn "Unable to retrieve the RP1-GPCLK-DKMS installer helper from $helper_url."
            rm -rf -- "$state_dir"
            return 1
        fi
        exec_command "Protect RP1 installer helper" chmod 0700 "$helper" "$debug" || return 1
    fi
    local update_args=(prepare-runtime-removal)
    local failure_output_file="$state_dir/runtime-removal-output.log"
    if [[ "$debug" == "debug" ]]; then
        update_args+=(--debug)
    fi
    if ! EXEC_COMMAND_FAILURE_OUTPUT_FILE="$failure_output_file" \
        exec_command "Prepare owned RP1 runtime for removal" \
        python3 "$helper" "${update_args[@]}" "$debug"; then
        warn "RP1-GPCLK-DKMS runtime recovery failed before application teardown."
        rm -rf -- "$state_dir"
        return 1
    fi
    rm -rf -- "$state_dir"
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Remove an unchanged RP1 provider only when a v2 record proves that
#        WsprryPi installed it.
# @details The helper preserves missing, legacy, malformed, drifted, active, or
#          otherwise ambiguous provider state. Release removal uses the normal
#          Debian package lifecycle; development removal uses the exact
#          upstream rollback entrypoint captured at installation time.
# -----------------------------------------------------------------------------
remove_owned_rp1_gpclk_dkms_provider() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    [[ "$ACTION" == "uninstall" ]] || return 0
    case "$REMOVE_RP1_GPCLK_DKMS" in
        auto|true|false) ;;
        *)
            warn "REMOVE_RP1_GPCLK_DKMS must be auto, true, or false."
            return 1
            ;;
    esac
    if [[ "$REMOVE_RP1_GPCLK_DKMS" == "false" ]]; then
        RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN="false"
        logI "RP1-GPCLK-DKMS preserved by explicit operator opt-out."
        debug_end "$debug"
        return 0
    fi

    local script_source="${BASH_SOURCE[0]:-}"
    local local_helper=""
    if [[ -n "$script_source" && "$script_source" != "bash" ]]; then
        local_helper="$(cd "$(dirname "$script_source")" && pwd -P)/rp1_gpclk_dkms_install.py"
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        RP1_GPCLK_DKMS_STATE_DIR="dry-run:no-state-created"
        RP1_GPCLK_DKMS_HELPER="${local_helper:-dry-run:no-helper-downloaded}"
    else
        RP1_GPCLK_DKMS_STATE_DIR=$(mktemp -d /tmp/wsprrypi-rp1-gpclk-dkms.XXXXXX)
        exec_command "Protect RP1 helper staging directory" chmod 0700 "$RP1_GPCLK_DKMS_STATE_DIR" "$debug" || return 1
        RP1_GPCLK_DKMS_HELPER="$RP1_GPCLK_DKMS_STATE_DIR/rp1_gpclk_dkms_install.py"
        if [[ -n "$local_helper" && -f "$local_helper" && ! -L "$local_helper" ]]; then
            exec_command "Stage RP1 installer helper" cp -- "$local_helper" "$RP1_GPCLK_DKMS_HELPER" "$debug" || return 1
            exec_command "Protect RP1 installer helper" chmod 0700 "$RP1_GPCLK_DKMS_HELPER" "$debug" || return 1
        else
            local helper_url="${GIT_RAW_BASE}/${REPO_ORG}/${REPO_NAME}/${REPO_BRANCH}/scripts/rp1_gpclk_dkms_install.py"
            if ! exec_command "Download RP1 installer helper" curl --proto '=https' --tlsv1.2 --fail --silent --show-error \
                --output "$RP1_GPCLK_DKMS_HELPER" "$helper_url" "$debug"; then
                warn "Unable to retrieve the RP1-GPCLK-DKMS installer helper from $helper_url."
                cleanup_rp1_gpclk_dkms_state
                return 1
            fi
            exec_command "Protect RP1 installer helper" chmod 0700 "$RP1_GPCLK_DKMS_HELPER" "$debug" || return 1
        fi
    fi

    local remove_args=(remove --remove "$REMOVE_RP1_GPCLK_DKMS")
    local removal_output_file=""
    if [[ "$debug" == "debug" ]]; then
        remove_args+=(--debug)
    fi
    if [[ "$DRY_RUN" != "true" ]]; then
        removal_output_file="$RP1_GPCLK_DKMS_STATE_DIR/provider-removal-output.log"
    fi
    if ! EXEC_COMMAND_FAILURE_OUTPUT_FILE="$removal_output_file" \
        exec_command "Check owned RP1 provider" \
            python3 "$RP1_GPCLK_DKMS_HELPER" "${remove_args[@]}" "$debug"; then
        warn "RP1-GPCLK-DKMS owned-provider removal failed; the ownership record was retained for recovery."
        cleanup_rp1_gpclk_dkms_state
        return 1
    fi
    if [[ "$DRY_RUN" != "true" ]]; then
        if grep -Fq "RP1-GPCLK-DKMS preserved:" "$removal_output_file"; then
            RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN="true"
        elif grep -Fq \
            "WsprryPi-owned RP1-GPCLK-DKMS provider removed and absence verified." \
            "$removal_output_file"; then
            RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN="false"
        elif grep -Fq \
            "RP1-GPCLK-DKMS absent: no provider removal was required." \
            "$removal_output_file"; then
            RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN="false"
        else
            warn "RP1-GPCLK-DKMS removal helper did not report a recognized outcome."
            cleanup_rp1_gpclk_dkms_state
            return 1
        fi
    fi
    cleanup_rp1_gpclk_dkms_state
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Print the system information to the log.
# @details Extracts and logs the system's name and version using information
#          from `/etc/os-release`. If the information cannot be extracted, logs
#          a warning message. Includes debug output when the `debug` argument is provided.
#
# @param $1 [Optional] Debug flag to enable detailed output (`debug`).
#
# @global None
#
# @return None
#
# @example
# print_system debug
# Outputs system information with debug logs enabled.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
print_system() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local system_name

    # Extract system name and version from /etc/os-release
    system_name=$(grep '^PRETTY_NAME=' /etc/os-release 2>/dev/null | cut -d '=' -f2 | tr -d '"')

    # Debug: Log extracted system name
    debug_print "Extracted system name: ${system_name:-<empty>}\n" "$debug"

    # Check if system_name is empty and log accordingly
    if [[ -z "${system_name:-}" ]]; then
        warn "System: Unknown (could not extract system information)."
        debug_print "System information could not be extracted." "$debug"
    else
        bitness=$(getconf LONG_BIT 2>/dev/null || printf "unknown")
        logI "System: $system_name on $bitness-bit OS." # Log the system information
        debug_print "Logged system information: $system_name on $bitness-bit OS" "$debug"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Print the script version and optionally log it.
# @details This function displays the version of the script stored in the global
#          variable `SEM_VER`. If called by `process_args`, it uses `printf` to
#          display the version; otherwise, it logs the version using `logI`.
#          If the debug flag is set to "debug," additional debug information
#          will be printed.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global THIS_SCRIPT The name of the script.
# @global SEM_VER The version of the script.
#
# @return None
#
# @example
# print_version debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
print_version() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    logI "Wsprry Pi version: ${SEM_VER}"

    debug_end "$debug"
    return 0
}

############
### Check Environment Functions
############

# -----------------------------------------------------------------------------
# @brief Determine the script's execution context.
# @details Identifies how the script was executed, returning one of the
#          predefined context codes. Handles errors gracefully and outputs
#          additional debugging information when the "debug" argument is
#          passed.
#
# Context Codes:
#   - `0`: Script executed via pipe.
#   - `1`: Script executed with `bash` in an unusual way.
#   - `2`: Script executed directly (local or from PATH).
#   - `3`: Script executed from within a GitHub repository.
#   - `4`: Script executed from a PATH location.
#
# @param $1 [Optional] Pass "debug" to enable verbose logging for debugging
#                      purposes.
#
# @throws Exits with an error if the script path cannot be resolved or
#         directory traversal exceeds the maximum depth.
#
# @return Returns a context code (described above) indicating the script's
#         execution context.
# # -----------------------------------------------------------------------------
determine_execution_context() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local script_path # Full path of the script
    local current_dir # Temporary variable to traverse directories

    # Check if the script is executed via pipe
    if [[ "$0" == "bash" ]]; then
        if [[ -p /dev/stdin ]]; then
            debug_print "Execution context: Script executed via pipe." "$debug"
            printf "0\n" # Execution via pipe
            debug_end "$debug"
            return 0
        else
            warn "Unusual bash execution detected."
            printf "1\n" # Unusual bash execution
            debug_end "$debug"
            return 0
        fi
    fi

    # Get the script path
    script_path=$(realpath "$0" 2>/dev/null) || script_path=$(pwd)/$(basename "$0")
    if [[ ! -f "$script_path" ]]; then
        debug_end "$debug"
        die 1 "Unable to resolve script path: $script_path"
    fi
    debug_print "Resolved script path: $script_path" "$debug"

    # Initialize current_dir with the directory part of script_path
    current_dir="${script_path%/*}"
    current_dir="${current_dir:-.}"

    # Safeguard against invalid current_dir during initialization
    if [[ ! -d "$current_dir" ]]; then
        debug_end "$debug"
        die 1 "Invalid starting directory: $current_dir"
    fi

    # Check if the current directory is inside a Git repository
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        debug_print "Script executed from inside a git repo." "$debug"
        printf "3\n" # Unusual bash execution
        debug_end "$debug"
        return 0
    fi

    # Check if the script is executed from a PATH location
    local resolved_path
    resolved_path=$(command -v "$(basename "$0")" 2>/dev/null)
    if [[ "$resolved_path" == "$script_path" ]]; then
        debug_print "Script executed from a PATH location: $resolved_path." "$debug"
        printf "4\n" # Executed from a PATH location
        debug_end "$debug"
        return 0
    fi

    # Default: Direct execution from the local filesystem
    debug_print "Default context: Script executed directly." "$debug"

    debug_end "$debug"
    printf "2\n" && return 0
}

# -----------------------------------------------------------------------------
# @brief Handle the execution context of the script.
# @details Determines the script's execution context by invoking
#          `determine_execution_context` and sets global variables based on
#          the context. Outputs debug information if the "debug" argument is
#          passed. Provides safeguards for unknown or invalid context codes.
#
# @param $1 [Optional] Pass "debug" to enable verbose logging for debugging
#                      purposes.
#
# @throws Exits with an error if an invalid context code is returned.
#
# @return None
# -----------------------------------------------------------------------------
handle_execution_context() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Call determine_execution_context and capture its output
    local context
    context=$(determine_execution_context "$debug")

    # Validate the context
    if ! [[ "$context" =~ ^[0-4]$ ]]; then
        debug_end "$debug"
        die 1 "Invalid context code returned: $context"
    fi

    # Initialize and set global variables based on the context
    case $context in
    0)
        THIS_SCRIPT="piped_script"
        # Only set DEBIAN_FRONTEND if it isn’t already set in the environment
        export DEBIAN_FRONTEND="${DEBIAN_FRONTEND:-noninteractive}"
        IS_REPO=false
        debug_print "Execution context: Script was piped (e.g., 'curl url | sudo bash')." "$debug"
        ;;
    1)
        THIS_SCRIPT="piped_script"
        # Only set DEBIAN_FRONTEND if it isn’t already set in the environment
        export DEBIAN_FRONTEND="${DEBIAN_FRONTEND:-noninteractive}"
        IS_REPO=false
        warn "Execution context: Script run with 'bash' in an unusual way."
        ;;
    2)
        THIS_SCRIPT=$(basename "$0")
        IS_REPO=false
        debug_print "Execution context: Script executed directly from $THIS_SCRIPT." "$debug"
        ;;
    3)
        THIS_SCRIPT=$(basename "$0")
        IS_REPO=true
        debug_print "Execution context: Script is within a GitHub repository." "$debug"
        ;;
    4)
        THIS_SCRIPT=$(basename "$0")
        IS_REPO=false
        debug_print "Execution context: Script executed from a PATH location ($(command -v "$THIS_SCRIPT"))" "$debug"
        ;;
    *)
        debug_end "$debug"
        die 99 "Unknown execution context."
        ;;
    esac

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Enforce that the script is run directly with `sudo`.
# @details Ensures the script is executed with `sudo` privileges and not:
#          - From a `sudo su` shell.
#          - As the root user directly.
#
# @global REQUIRE_SUDO Boolean indicating if `sudo` is required.
# @global SUDO_USER User invoking `sudo`.
# @global SUDO_COMMAND The command invoked with `sudo`.
# @global THIS_SCRIPT Name of the current script.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @return None
# @exit 1 if the script is not executed correctly.
#
# @example
# enforce_sudo debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
enforce_sudo() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    local arg

    if [[ "$REQUIRE_SUDO" == true ]]; then
        for arg in "$@"; do
            case "$arg" in
                -h|--help|-v|--version)
                    debug_end "$debug"
                    return 0
                    ;;
            esac
        done

        if [[ "$EUID" -eq 0 && -n "$SUDO_USER" && "$SUDO_COMMAND" == *"$0"* ]]; then
            debug_print "Sudo conditions met. Proceeding." "$debug"
            # Script is properly executed with `sudo`
        elif [[ "$EUID" -eq 0 && -n "$SUDO_USER" ]]; then
            debug_print "Script run from a root shell. Exiting." "$debug"
            die 1 "This script should not be run from a root shell." \
                "Run it with 'sudo $THIS_SCRIPT' as a regular user."
        elif [[ "$EUID" -eq 0 ]]; then
            debug_print "Script run as root. Exiting." "$debug"
            die 1 "This script should not be run as the root user." \
                "Run it with 'sudo $THIS_SCRIPT' as a regular user."
        else
            debug_print "Script not run with sudo. Exiting." "$debug"
            die 1 "This script requires 'sudo' privileges." \
                "Please re-run it using 'sudo $THIS_SCRIPT'."
        fi
    fi

    debug_print "Function parameters:" \
        "\n\t- REQUIRE_SUDO='${REQUIRE_SUDO:-(not set)}'" \
        "\n\t- EUID='$EUID'" \
        "\n\t- SUDO_USER='${SUDO_USER:-(not set)}'" \
        "\n\t- SUDO_COMMAND='${SUDO_COMMAND:-(not set)}'" "$debug"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check for required dependencies and report any missing ones.
# @details Iterates through the dependencies listed in the global array
#          `DEPENDENCIES`, checking if each one is installed. Logs missing
#          dependencies and exits the script with an error code if any are
#          missing.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global DEPENDENCIES Array of required dependencies.
#
# @return None
# @exit 1 if any dependencies are missing.
#
# @example
# validate_depends debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
validate_depends() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local missing=0 # Counter for missing dependencies
    local dep       # Iterator for dependencies

    # Iterate through dependencies
    for dep in "${DEPENDENCIES[@]}"; do
        if ! command -v "$dep" &>/dev/null; then
            warn "Missing dependency: $dep"
            ((missing++))
            debug_print "Missing dependency: $dep" "$debug"
        else
            debug_print "Found dependency: $dep" "$debug"
        fi
    done

    # Handle missing dependencies
    if ((missing > 0)); then
        debug_end "$debug"
        die 1 "Missing $missing dependencies. Install them and re-run the script."
    fi

    debug_print "All dependencies are present." "$debug"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check the availability of critical system files.
# @details Verifies that each file listed in the `SYSTEM_READS` array exists
#          and is readable. Logs an error for any missing or unreadable files
#          and exits the script if any issues are found.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global SYSTEM_READS Array of critical system file paths to check.
#
# @return None
# @exit 1 if any required files are missing or unreadable.
#
# @example
# validate_sys_accs debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
validate_sys_accs() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local missing=0 # Counter for missing or unreadable files
    local file      # Iterator for files

    # Iterate through system files
    for file in "${SYSTEM_READS[@]}"; do
        if [[ ! -r "$file" ]]; then
            if [[ "$file" == "/proc/device-tree/compatible" && \
                "$INSTALL_RP1_GPCLK_DKMS" == "true" ]]; then
                warn "Missing or unreadable platform identity: $file; continuing because RP1 provider installation was explicitly requested."
                debug_print "Explicit RP1 provider override permits missing platform identity: $file" "$debug"
                continue
            fi
            warn "Missing or unreadable file: $file"
            ((missing++))
            debug_print "Missing or unreadable file: $file" "$debug"
        else
            debug_print "File is accessible: $file" "$debug"
        fi
    done

    # Handle missing files
    if ((missing > 0)); then
        debug_end "$debug"
        die 1 "Missing or unreadable $missing critical system files."
    fi

    debug_print "All critical system files are accessible." "$debug"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Validate the existence of required environment variables.
# @details Checks if the environment variables specified in the `ENV_VARS`
#          array are set. Logs any missing variables and exits the script if
#          any are missing.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global ENV_VARS Array of required environment variables.
#
# @return None
# @exit 1 if any environment variables are missing.
#
# @example
# validate_env_vars debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
validate_env_vars() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local missing=0 # Counter for missing environment variables
    local var       # Iterator for environment variables

    # Iterate through environment variables
    for var in "${ENV_VARS[@]}"; do
        if [[ -z "${!var:-}" ]]; then
            printf "ERROR: Missing environment variable: %s\n" "$var" >&2
            ((missing++))
            debug_print "Missing environment variable: $var" "$debug"
        else
            debug_print "Environment variable is set: $var=${!var}" "$debug"
        fi
    done

    # Handle missing variables
    if ((missing > 0)); then
        printf "ERROR: Missing %d required environment variables. Ensure all required environment variables are set and re-run the script.\n" "$missing" >&2
        debug_end "$debug"
        exit 1
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check if the script is running in a Bash shell.
# @details Ensures the script is executed with Bash, as it may use Bash-
#          specific features. If the "debug" argument is passed, detailed
#          logging will be displayed for each check.
#
# @param $1 [Optional] "debug" to enable verbose output for all checks.
#
# @global BASH_VERSION The version of the Bash shell being used.
#
# @return None
# @exit 1 if not running in Bash.
#
# @example
# check_bash
# check_bash debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
check_bash() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Ensure the script is running in a Bash shell
    if [[ -z "${BASH_VERSION:-}" ]]; then
        debug_end "$debug"
        die 1 "This script requires Bash. Please run it with Bash."
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check if the current Bash version meets the minimum required version.
# @details Compares the current Bash version against a required version
#          specified in the global variable `MIN_BASH_VERSION`. If
#          `MIN_BASH_VERSION` is "none", the check is skipped. Outputs debug
#          information if enabled.
#
# @param $1 [Optional] "debug" to enable verbose output for this check.
#
# @global MIN_BASH_VERSION Minimum required Bash version (e.g., "4.0") or
#                          "none".
# @global BASH_VERSINFO Array containing the major and minor versions of the
#         running Bash.
#
# @return None
# @exit 1 if the Bash version is insufficient.
#
# @example
# check_sh_ver
# check_sh_ver debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
check_sh_ver() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local required_version="${MIN_BASH_VERSION:-none}"

    # If MIN_BASH_VERSION is "none", skip version check
    if [[ "$required_version" == "none" ]]; then
        debug_print "Bash version check is disabled (MIN_BASH_VERSION='none')." "$debug"
    else
        debug_print "Minimum required Bash version is set to '$required_version'." "$debug"

        # Extract the major and minor version components from the required version
        local required_major="${required_version%%.*}"
        local required_minor="${required_version#*.}"
        required_minor="${required_minor%%.*}"

        # Log current Bash version for debugging
        debug_print "Current Bash version is ${BASH_VERSINFO[0]}.${BASH_VERSINFO[1]}." "$debug"

        # Compare the current Bash version with the required version
        if ((BASH_VERSINFO[0] < required_major || (\
            BASH_VERSINFO[0] == required_major && BASH_VERSINFO[1] < required_minor))); then
            debug_end "$debug"
            die 1 "This script requires Bash version $required_version or newer."
        fi
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check system bitness compatibility.
# @details Validates whether the current system's bitness matches the supported
#          configuration. Outputs debug information if debug mode is enabled.
#
# @param $1 [Optional] "debug" to enable verbose output for the check.
#
# @global SUPPORTED_BITNESS Specifies the bitness supported by the script
#         ("32", "64", or "both").
#
# @return None
# @exit 1 if the system bitness is unsupported.
#
# @example
# check_bitness
# check_bitness debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
check_bitness() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local bitness # Stores the detected bitness of the system.

    # Detect the system bitness
    bitness=$(getconf LONG_BIT)

    # Debugging: Detected system bitness
    debug_print "Detected system bitness: $bitness-bit." "$debug"

    case "$SUPPORTED_BITNESS" in
    "32")
        debug_print "Script supports only 32-bit systems." "$debug"
        if [[ "$bitness" -ne 32 ]]; then
            debug_end "$debug"
            die 1 "Only 32-bit systems are supported. Detected $bitness-bit system."
        fi
        ;;
    "64")
        debug_print "Script supports only 64-bit systems." "$debug"
        if [[ "$bitness" -ne 64 ]]; then
            debug_end "$debug"
            die 1 "Only 64-bit systems are supported. Detected $bitness-bit system."
        fi
        ;;
    "both")
        debug_print "Script supports both 32-bit and 64-bit systems." "$debug"
        ;;
    *)
        debug_print "Invalid SUPPORTED_BITNESS configuration: '$SUPPORTED_BITNESS'." "$debug"
        debug_end "$debug"
        die 1 "Configuration error: Invalid value for SUPPORTED_BITNESS ('$SUPPORTED_BITNESS')."
        ;;
    esac

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check Raspbian OS version compatibility.
# @details This function ensures that the Raspbian version is within the
#          supported range and logs an error if the compatibility check fails.
#
# @param $1 [Optional] "debug" to enable verbose output for this check.
#
# @global MIN_OS Minimum supported OS version.
# @global MAX_OS Maximum supported OS version (-1 indicates no upper limit).
# @global log_message Function for logging messages.
# @global die Function to handle critical errors and terminate the script.
#
# @return None Exits the script with an error code if the OS version is
#         incompatible.
#
# @example
# check_release
# check_release debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
check_release() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local ver # Holds the extracted version ID from /etc/os-release.

    # Ensure the file exists and is readable.
    if [[ ! -f /etc/os-release || ! -r /etc/os-release ]]; then
        die 1 "Unable to read /etc/os-release. Ensure this script is run on a compatible system."
    fi

    # Extract the VERSION_ID from /etc/os-release.
    if [[ -f /etc/os-release ]]; then
        ver=$(grep "VERSION_ID" /etc/os-release | awk -F "=" '{print $2}' | tr -d '"')
    else
        warn "File /etc/os-release not found."
        ver="unknown"
    fi
    debug_print "Raspbian version '$ver' detected." "$debug"

    # Ensure the extracted version is not empty.
    if [[ -z "${ver:-}" ]]; then
        debug_end "$debug"
        die 1 "VERSION_ID is missing or empty in /etc/os-release."
    fi

    # Check if the version is older than the minimum supported version.
    if [[ "$ver" -lt "$MIN_OS" ]]; then
        debug_end "$debug"
        die 1 "Raspbian version $ver is older than the minimum supported version ($MIN_OS)."
    fi

    # Check if the version is newer than the maximum supported version, if applicable.
    if [[ "$MAX_OS" -ne -1 && "$ver" -gt "$MAX_OS" ]]; then
        debug_end "$debug"
        die 1 "Raspbian version $ver is newer than the maximum supported version ($MAX_OS)."
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check if the detected Raspberry Pi model is supported.
# @details Reads the Raspberry Pi model from /proc/device-tree/compatible and
#          checks it against a predefined list of supported models. Logs an
#          error if the model is unsupported or cannot be detected. Optionally
#          outputs debug information about all models if `debug` is set to
#          `true`.
#
# @param $1 [Optional] "debug" to enable verbose output for all supported/
#                      unsupported models.
#
# @global SUPPORTED_MODELS Associative array of supported and unsupported
#         Raspberry Pi models.
# @global log_message Function for logging messages.
# @global die Function to handle critical errors and terminate the script.
#
# @return None Exits the script with an error code if the architecture is
#         unsupported.
#
# @example
# check_arch
# check_arch debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
check_arch() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local detected_model key full_name model chip
    local is_supported=false

    # Safely check if SUPPORTED_MODELS is set and if it allows all models
    if [[ "${SUPPORTED_MODELS[all]-}" == "Supported" ]]; then
        is_supported=true
        debug_print "All models are allowed, SUPPORTED_MODELS is set to 'all'" "$debug"
        debug_end "$debug"
        return 0
    fi

    # Read and process the compatible string
    if ! detected_model=$(tr '\0' '\n' </proc/device-tree/compatible 2>/dev/null | sed -n 's/raspberrypi,//p'); then
        if [[ "$INSTALL_RP1_GPCLK_DKMS" == "true" ]]; then
            warn "Platform architecture detection is unavailable; continuing because RP1 provider installation was explicitly requested."
            debug_end "$debug"
            return 0
        fi
        debug_end "$debug"
        die 1 "Failed to read or process /proc/device-tree/compatible. Ensure compatibility."
    fi

    # Check if the detected model is empty
    if [[ -z "${detected_model:-}" ]]; then
        if [[ "$INSTALL_RP1_GPCLK_DKMS" == "true" ]]; then
            warn "This system has no recognized Raspberry Pi architecture; continuing because RP1 provider installation was explicitly requested."
            debug_end "$debug"
            return 0
        fi
        debug_end "$debug"
        die 1 "No Raspberry Pi model found in /proc/device-tree/compatible. This system may not be supported."
    fi
    debug_print "Detected model: $detected_model" "$debug"

    # Iterate through supported models to check compatibility
    for key in "${!SUPPORTED_MODELS[@]}"; do
        IFS='|' read -r full_name model chip <<<"$key"
        if [[ "$model" == "$detected_model" ]]; then
            if [[ "${SUPPORTED_MODELS[$key]}" == "Supported" ]]; then
                is_supported=true
                debug_print "Model: '$full_name' ($chip) is supported." "$debug"
            else
                if [[ "$INSTALL_RP1_GPCLK_DKMS" == "true" ]]; then
                    warn "Model '$full_name' ($chip) is normally unsupported; continuing because RP1 provider installation was explicitly requested."
                    debug_end "$debug"
                    return 0
                fi
                debug_end "$debug"
                die 1 "Model: '$full_name' ($chip) is not supported."
            fi
            break
        fi
    done

    # Debug output of all models if requested
    if [[ "$debug" == "debug" ]]; then
        # Arrays to hold supported and unsupported models
        declare -a supported_models=()
        declare -a unsupported_models=()

        # Group the models into supported and unsupported
        for key in "${!SUPPORTED_MODELS[@]}"; do
            IFS='|' read -r full_name model chip <<<"$key"
            if [[ "${SUPPORTED_MODELS[$key]}" == "Supported" ]]; then
                supported_models+=("$full_name ($chip)")
            else
                unsupported_models+=("$full_name ($chip)")
            fi
        done

        # Sort and print supported models
        if [[ ${#supported_models[@]} -gt 0 ]]; then
            printf "[DEBUG] Supported models:\n" >&2
            for model in $(printf "%s\n" "${supported_models[@]}" | sort); do
                printf "\t- %s\n" "$model" >&2
            done
        fi

        # Sort and print unsupported models
        if [[ ${#unsupported_models[@]} -gt 0 ]]; then
            printf "[DEBUG] Unsupported models:\n" >&2
            for model in $(printf "%s\n" "${unsupported_models[@]}" | sort); do
                printf "\t- %s\n" "$model" >&2
            done
        fi
    fi

    # Log an error if no supported model was found
    if [[ "$is_supported" == false ]]; then
        if [[ "$INSTALL_RP1_GPCLK_DKMS" == "true" ]]; then
            warn "Detected Raspberry Pi model '$detected_model' is not recognized or supported; continuing because RP1 provider installation was explicitly requested."
            debug_end "$debug"
            return 0
        fi
        debug_end "$debug"
        die 1 "Detected Raspberry Pi model '$detected_model' is not recognized or supported."
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Validate proxy connectivity by testing a known URL.
# @details Uses `check_url` to verify connectivity through the provided proxy
#          settings.
#
# @param $1 [Optional] Proxy URL to validate (defaults to `http_proxy` or
#                      `https_proxy` if not provided).
# @param $2 [Optional] "debug" to enable verbose output for the proxy
#                      validation.
#
# @global http_proxy The HTTP proxy URL (if set).
# @global https_proxy The HTTPS proxy URL (if set).
#
# @return 0 if the proxy is functional, 1 otherwise.
#
# @example
# validate_proxy "http://myproxy.com:8080"
# validate_proxy debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
validate_proxy() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Check if proxy_url is passed
    local proxy_url=""
    # Check if proxy_url is the first argument (if set)
    if [[ -n "$1" && "$1" =~ ^https?:// ]]; then
        # First argument is proxy_url
        proxy_url="$1"
        shift # Move to the next argument
    fi

    # Default to global proxy settings if no proxy is provided
    [[ -z "${proxy_url:-}" ]] && proxy_url="${http_proxy:-$https_proxy}"

    # Validate that a proxy is set
    if [[ -z "${proxy_url:-}" ]]; then
        warn "No proxy URL configured for validation."
        debug_end "$debug"
        return 1
    fi

    logI "Validating proxy: $proxy_url"

    # Test the proxy connectivity using check_url (passing the debug flag)
    if check_url "$proxy_url" "curl" "--silent --head --max-time 10 --proxy $proxy_url" "$debug"; then
        logI "Proxy $proxy_url is functional."
        debug_print "Proxy $proxy_url is functional." "$debug"
        debug_end "$debug"
        return 0
    else
        warn "Proxy $proxy_url is unreachable or misconfigured."
        debug_print "Proxy $proxy_url failed validation." "$debug"
        debug_end "$debug"
        return 1
    fi
}

# -----------------------------------------------------------------------------
# @brief Check connectivity to a URL using a specified tool.
# @details Attempts to connect to a given URL with `curl` or `wget` based on
#          the provided arguments. Ensures that the tool's availability is
#          checked and handles timeouts gracefully. Optionally prints debug
#          information if the "debug" flag is set.
#
# @param $1 The URL to test.
# @param $2 The tool to use for the test (`curl` or `wget`).
# @param $3 Options to pass to the testing tool (e.g., `--silent --head` for
#           `curl`).
# @param $4 [Optional] "debug" to enable verbose output during the check.
#
# @return 0 if the URL is reachable, 1 otherwise.
#
# @example
# check_url "http://example.com" "curl" "--silent --head" debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
check_url() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local url="$1"
    local tool="$2"
    local options="$3"

    # Validate inputs
    if [[ -z "${url:-}" ]]; then
        printf "ERROR: URL and tool parameters are required for check_url.\n" >&2
        debug_end "$debug"
        return 1
    fi

    # Check tool availability
    if ! command -v "$tool" &>/dev/null; then
        printf "ERROR: Tool '%s' is not installed or unavailable.\n" "$tool" >&2
        debug_end "$debug"
        return 1
    fi

    # Perform the connectivity check, allowing SSL and proxy errors
    local retval
    # shellcheck disable=2086
    if $tool $options "$url" &>/dev/null; then
        debug_print "Successfully connected to $#url using $tool." "$debug"
        retval=0
    else
        debug_print "Failed to connect to $url using $tool." "$debug"
        retval=1
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Comprehensive internet and proxy connectivity check.
# @details Combines proxy validation and direct internet connectivity tests
#          using `check_url`. Validates proxy configuration first, then tests
#          connectivity with and without proxies. Outputs debug information if
#          enabled.
#
# @param $1 [Optional] "debug" to enable verbose output for all checks.
#
# @global http_proxy Proxy URL for HTTP (if set).
# @global https_proxy Proxy URL for HTTPS (if set).
# @global no_proxy Proxy exclusions (if set).
#
# @return 0 if all tests pass, 1 if any test fails.
#
# @example
# check_internet debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
check_internet() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local primary_url="http://google.com"
    local secondary_url="http://1.1.1.1"
    local proxy_valid=false

    # Validate proxy settings
    if [[ -n "${http_proxy:-}" || -n "${https_proxy:-}" ]]; then
        debug_print "Proxy detected. Validating proxy configuration." "$debug"
        if validate_proxy "$debug"; then # Pass debug flag to validate_proxy
            proxy_valid=true
            debug_print "Proxy validation succeeded." "$debug"
        else
            warn "Proxy validation failed. Proceeding with direct connectivity checks."
        fi
    fi

    # Check connectivity using curl
    if command -v curl &>/dev/null; then
        debug_print "curl is available. Testing internet connectivity using curl." "$debug"

        # Check with proxy
        if $proxy_valid && curl --silent --head --max-time 10 --proxy "${http_proxy:-${https_proxy:-}}" "$primary_url" &>/dev/null; then
            logI "Internet is available using curl with proxy."
            debug_print "curl successfully connected via proxy." "$debug"
            debug_end "$debug"
            return 0
        fi

        # Check without proxy
        if curl --silent --head --max-time 10 "$primary_url" &>/dev/null; then
            debug_print "curl successfully connected without proxy." "$debug"
            debug_end "$debug"
            return 0
        fi

        debug_print "curl failed to connect." "$debug"
    else
        debug_print "curl is not available." "$debug"
    fi

    # Check connectivity using wget
    if command -v wget &>/dev/null; then
        debug_print "wget is available. Testing internet connectivity using wget." "$debug"

        # Check with proxy
        if $proxy_valid && wget --spider --quiet --timeout=10 --proxy="${http_proxy:-${https_proxy:-}}" "$primary_url" &>/dev/null; then
            logI "Internet is available using wget with proxy."
            debug_print "wget successfully connected via proxy." "$debug"
            debug_end "$debug"
            return 0
        fi

        # Check without proxy
        if wget --spider --quiet --timeout=10 "$secondary_url" &>/dev/null; then
            logI "Internet is available using wget without proxy."
            debug_print "wget successfully connected without proxy." "$debug"
            debug_end "$debug"
            return 0
        fi

        debug_print "wget failed to connect." "$debug"
    else
        debug_print "wget is not available." "$debug"
    fi

    # Final failure message
    warn "No internet connection detected after all checks."
    debug_print "All internet connectivity tests failed." "$debug"
    debug_end "$debug"
    return 1
}

############
### Logging Functions
############

# -----------------------------------------------------------------------------
# @brief Pads a number with leading spaces to achieve the desired width.
# @details This function takes a number and a specified width, and returns the
#          number formatted with leading spaces if necessary. The number is
#          guaranteed to be a valid non-negative integer, and the width is
#          checked to ensure it is a positive integer. If "debug" is passed as
#          the second argument, it defaults the width to 4 and provides debug
#          information.
#
# @param $1 [required] The number to be padded (non-negative integer).
# @param $2 [optional] The width of the output (defaults to 4 if not provided).
#
# @return 0 on success.
#
# @example
# pad_with_spaces 42 6  # Output: "   42"
# pad_with_spaces 123 5  # Output: "  123"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
pad_with_spaces() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare locals
    local number="${1:-0}" # Input number (mandatory)
    local width="${2:-4}"  # Optional width (default is 4)

    # If the second parameter is "debug", adjust the arguments
    if [[ "$width" == "debug" ]]; then
        debug="$width"
        width=4 # Default width to 4 if "debug" was passed in place of width
    fi

    # Validate input for the number
    if [[ -z "${number:-}" || ! "$number" =~ ^[0-9]+$ ]]; then
        die 1 "Input must be a valid non-negative integer."
    fi

    # Ensure the width is a positive integer
    if [[ ! "$width" =~ ^[0-9]+$ || "$width" -lt 1 ]]; then
        die 1 "Error: Width must be a positive integer."
    fi

    # Strip leading zeroes to prevent octal interpretation
    number=$((10#$number)) # Forces the number to be interpreted as base-10

    # Format the number with leading spaces and return it as a string
    printf "%${width}d\n" "$number"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Wraps a message into lines with ellipses for overflow or continuation.
# @details This function splits the message into lines, appending an ellipsis
#          for overflowed lines and prepending it for continuation lines. The
#          primary and secondary messages are processed separately and combined
#          with a delimiter.
#
# @param $1 [required] The message string to wrap.
# @param $2 [required] Maximum width of each line (numeric).
# @param $3 [optional] The secondary message string to include (defaults to
#                      an empty string).
#
# @global None.
#
# @throws None.
#
# @return A single string with wrapped lines and ellipses added as necessary.
#         The primary and secondary messages are separated by a delimiter.
#
# @example
# wrapped=$(wrap_messages "This is a long message" 50)
# echo "$wrapped"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
wrap_messages() {
    local line_width=$1
    local primary=$2
    local secondary=${3:-}
    local delimiter="␞"

    # Validate input
    if [[ -z "$line_width" || ! "$line_width" =~ ^[0-9]+$ || "$line_width" -le 1 ]]; then
        printf "Error: Invalid line width '%s' in %s(). Must be a positive integer.\n" \
            "$line_width" "${FUNCNAME[0]}" >&2
        return 1
    fi

    # Inner function to wrap a single message
    wrap_message() {
        local message=$1
        local width=$2
        local result=()
        # Address faulty width with a min of 10
        local adjusted_width=$((width > 10 ? width - 1 : 10))

        while IFS= read -r line; do
            result+=("$line")
        done <<<"$(printf "%s\n" "$message" | fold -s -w "$adjusted_width")"

        for ((i = 0; i < ${#result[@]}; i++)); do
            result[i]=$(printf "%s" "${result[i]}" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
            if ((i == 0)); then
                result[i]="${result[i]}…"
            elif ((i == ${#result[@]} - 1)); then
                result[i]="…${result[i]}"
            else
                result[i]="…${result[i]}…"
            fi
        done

        printf "%s\n" "${result[@]}"
    }

    # Process primary message
    local overflow=""
    if [[ ${#primary} -gt $line_width ]]; then
        local wrapped_primary
        wrapped_primary=$(wrap_message "$primary" "$line_width")
        overflow=$(printf "%s\n" "$wrapped_primary" | tail -n +2)
        primary=$(printf "%s\n" "$wrapped_primary" | head -n 1)
    fi

    # Process secondary message
    if [[ -n ${#secondary} && ${#secondary} -gt $line_width ]]; then
        secondary=$(wrap_message "$secondary" "$line_width")
    fi

    # Combine results
    printf "%s%b%s%b%s" \
        "$primary" \
        "$delimiter" \
        "$overflow" \
        "$delimiter" \
        "$secondary"
}

# -----------------------------------------------------------------------------
# @brief Log a message with optional details to the console and/or file.
# @details Handles combined logic for logging to console and/or file,
#          supporting optional details. If details are provided, they are
#          logged with an "[EXTENDED]" tag.
#
# @param $1 Timestamp of the log entry.
# @param $2 Log level (e.g., DEBUG, INFO, WARN, ERROR).
# @param $3 Color code for the log level.
# @param $4 Line number where the log entry originated.
# @param $5 The main log message.
# @param $6 [Optional] Additional details for the log entry.
#
# @global LOG_OUTPUT Specifies where to output logs ("console", "file", or
#         "both").
# @global LOG_FILE File path for log storage if `LOG_OUTPUT` includes "file".
# @global THIS_SCRIPT The name of the current script.
# @global RESET ANSI escape code to reset text formatting.
#
# @return None
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
print_log_entry() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables at the start of the function
    local timestamp="$1"
    local level="$2"
    local color="$3"
    local lineno="$4"
    local message="$5"

    # Skip logging if the message is empty
    if [[ -z "$message" ]]; then
        debug_end "$debug"
        return 1
    fi

    # Log to file if required
    if [[ "$LOG_OUTPUT" == "file" || "$LOG_OUTPUT" == "both" ]]; then
        printf "%s [%s] [%s:%d] %s\\n" "$timestamp" "$level" "$THIS_SCRIPT" "$lineno" "$message" >>"$LOG_FILE"
    fi

    # Log to console if required and USE_CONSOLE is true
    if [[ "$USE_CONSOLE" == "true" && ("$LOG_OUTPUT" == "console" || "$LOG_OUTPUT" == "both") ]]; then
        printf "%b[%s]%b %s\\n" "$color" "$level" "$RESET" "$message"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Generate a timestamp and line number for log entries.
#
# @details This function retrieves the current timestamp and the line number of
#          the calling script. If the optional debug flag is provided, it will
#          print debug information, including the function name, caller's name,
#          and the line number where the function was called.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @return A pipe-separated string in the format: "timestamp|line_number".
#
# @example
# prepare_log_context "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
prepare_log_context() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local timestamp
    local lineno

    # Generate the current timestamp
    timestamp=$(date "+%Y-%m-%d %H:%M:%S")

    # Retrieve the line number of the caller
    lineno="${BASH_LINENO[2]}"

    # Pass debug flag to pad_with_spaces
    lineno=$(pad_with_spaces "$lineno" "$debug") # Pass debug flag

    # Return the pipe-separated timestamp and line number
    printf "%s|%s\n" "$timestamp" "$lineno"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Log a message with the specified log level.
# @details Logs messages to both the console and/or a log file, depending on
#          the configured log output. The function uses the `LOG_PROPERTIES`
#          associative array to determine the log level, color, and severity.
#          If the "debug" argument is provided, debug logging is enabled for
#          additional details.
#
# @param $1 Log level (e.g., DEBUG, INFO, ERROR). The log level controls the
#           message severity.
# @param $2 Main log message to log.
# @param $3 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global LOG_LEVEL The current logging verbosity level.
# @global LOG_PROPERTIES Associative array defining log level properties, such
#         as severity and color.
# @global LOG_FILE Path to the log file (if configured).
# @global USE_CONSOLE Boolean flag to enable or disable console output.
# @global LOG_OUTPUT Specifies where to log messages ("file", "console",
#         "both").
#
# @return None
#
# @example
# log_message "INFO" "This is a message"
# log_message "INFO" "This is a message" "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
log_message() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Ensure the calling function is log_message_with_severity()
    if [[ "${FUNCNAME[1]}" != "log_message_with_severity" ]]; then
        warn "log_message() can only be called from log_message_with_severity()."
        debug_end "$debug"
        return 1
    fi

    local level="UNSET"
    local message="<no message>"

    local context timestamp lineno custom_level color severity config_severity

    # Get level if it exists (must be one of the predefined values)
    if [[ -n "$1" && "$1" =~ ^(DEBUG|INFO|WARNING|ERROR|CRITICAL|EXTENDED)$ ]]; then
        level="$1"
        shift # Move to the next argument
    fi

    # Get message if it exists and is not "debug"
    if [[ -n "$1" ]]; then
        message="$1"
        shift # Move to the next argument
    fi

    # Validate the log level and message if needed
    if [[ "$level" == "UNSET" || -z "${LOG_PROPERTIES[$level]:-}" || "$message" == "<no message>" ]]; then
        warn "Invalid log level '$level' or empty message."
        debug_end "$debug"
        return 1
    fi

    # Prepare log context (timestamp and line number)
    context=$(prepare_log_context "$debug") # Pass debug flag to sub-function
    IFS="|" read -r timestamp lineno <<<"$context"

    # Extract log properties for the specified level
    IFS="|" read -r custom_level color severity <<<"${LOG_PROPERTIES[$level]}"

    # Check if all three values (custom_level, color, severity) were successfully parsed
    if [[ -z "$custom_level" || -z "$color" || -z "$severity" ]]; then
        warn "Malformed log properties for level '$level'. Using default values."
        custom_level="UNSET"
        color="$RESET"
        severity=0
    fi

    # Extract severity threshold for the configured log level
    IFS="|" read -r _ _ config_severity <<<"${LOG_PROPERTIES[$LOG_LEVEL]}"

    # Check for valid severity level
    if [[ -z "$config_severity" || ! "$config_severity" =~ ^[0-9]+$ ]]; then
        warn "Malformed severity value for level '$LOG_LEVEL'."
        debug_end "$debug"
        return 1
    fi

    # Skip logging if the message's severity is below the configured threshold
    if ((severity < config_severity)); then
        debug_end "$debug"
        return 0
    fi

    # Call print_log_entry to handle actual logging (to file and console)
    print_log_entry "$timestamp" "$custom_level" "$color" "$lineno" "$message" "$debug"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Log a message with the specified severity level.
# @details This function logs messages at the specified severity level and
#          handles extended details and debug information if provided.
#
# @param $1 Severity level (e.g., DEBUG, INFO, WARNING, ERROR, CRITICAL).
# @param $2 Main log message.
# @param $3 [Optional] Extended details for the log entry.
# @param $4 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @return None
#
# @example
# log_message_with_severity "ERROR" /
#   "This is an error message" "Additional details" "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
log_message_with_severity() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Exit if the calling function is not one of the allowed ones.
    # shellcheck disable=2076
    if [[ ! "logD logI logW logE logC logX" =~ "${FUNCNAME[1]}" ]]; then
        warn "Invalid calling function: ${FUNCNAME[1]}"
        debug_end "$debug"
        exit 1
    fi

    # Initialize variables
    local severity="INFO" # Default to INFO
    local message=""
    local extended_message=""

    # Get level if it exists (must be one of the predefined values)
    if [[ -n "${1:-}" && "${1:-}" =~ ^(DEBUG|INFO|WARNING|ERROR|CRITICAL|EXTENDED)$ ]]; then
        severity="${1:-}"
    fi

    # Process arguments
    if [[ -n "${2:-}" ]]; then
        message="${2:-}"
    else
        warn "Message is required."
        debug_end "$debug"
        return 1
    fi

    # Extended_message will be empty if $3 is unset
    extended_message="${3:-}"

    # Print debug information if the flag is set
    debug_print "Logging message at severity '$severity' with message='$message'." "$debug"
    debug_print "Extended message: '$extended_message'" "$debug"

    # Log the primary message
    log_message "$severity" "$message" "$debug"

    # Log the extended message if present
    if [[ -n "$extended_message" ]]; then
        logX "$extended_message" "$debug"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Logging wrapper functions for various severity levels.
# @details These functions provide shorthand access to
#          `log_message_with_severity()` with a predefined severity level. They
#          standardize the logging process by ensuring consistent severity
#          labels and argument handling.
#
# @param $1 [string] The primary log message. Must not be empty.
# @param $2 [optional, string] The extended message for additional details
#           (optional), sent to logX.
# @param $3 [optional, string] The debug flag. If set to "debug", enables
#           debug-level logging.
#
# @global None
#
# @return None
#
# @functions
# - logD(): Logs a message with severity level "DEBUG".
# - logI(): Logs a message with severity level "INFO".
# - logW(): Logs a message with severity level "WARNING".
# - logE(): Logs a message with severity level "ERROR".
# - logC(): Logs a message with severity level "CRITICAL".
# - logX(): Logs a message with severity level "EXTENDED".
#
# @example
#   logD "Debugging application startup."
#   logI "Application initialized successfully."
#   logW "Configuration file is missing a recommended value."
#   logE "Failed to connect to the database."
#   logC "System is out of memory and must shut down."
#   logX "Additional debug information for extended analysis."
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
logD() { log_message_with_severity "DEBUG" "${1:-}" "${2:-}" "${3:-}"; }
# shellcheck disable=SC2317
# shellcheck disable=SC2329
logI() { log_message_with_severity "INFO" "${1:-}" "${2:-}" "${3:-}"; }
# shellcheck disable=SC2317
# shellcheck disable=SC2329
logW() { log_message_with_severity "WARNING" "${1:-}" "${2:-}" "${3:-}"; }
# shellcheck disable=SC2317
# shellcheck disable=SC2329
logE() { log_message_with_severity "ERROR" "${1:-}" "${2:-}" "${3:-}"; }
# shellcheck disable=SC2317
# shellcheck disable=SC2329
logC() { log_message_with_severity "CRITICAL" "${1:-}" "${2:-}" "${3:-}"; }
# shellcheck disable=SC2317
# shellcheck disable=SC2329
logX() { log_message_with_severity "EXTENDED" "${1:-}" "${2:-}" "${3:-}"; }

# -----------------------------------------------------------------------------
# @brief Ensure the log file exists and is writable, with fallback to `/tmp` if
#        necessary.
# @details This function validates the specified log file's directory to ensure
#          it exists and is writable. If the directory is invalid or
#          inaccessible, it attempts to create it. If all else fails, the log
#          file is redirected to `/tmp`. A warning message is logged if
#          fallback is used.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global LOG_FILE Path to the log file (modifiable to fallback location).
# @global THIS_SCRIPT The name of the script (used to derive fallback log file
#         name).
#
# @return None
#
# @example
# init_log "debug"  # Ensures log file is created and available for writing
#                   # with debug output.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
init_log() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local scriptname="${THIS_SCRIPT%%.*}" # Extract script name without extension
    local homepath log_dir fallback_log

    # A dry run must not create, truncate, chown, or append to a log file.
    # Console output remains available, including debug output.
    if [[ "$DRY_RUN" == "true" ]]; then
        LOG_FILE="/dev/null"
        readonly LOG_FILE
        export LOG_FILE
        debug_print "Dry run: file logging disabled." "$debug"
        debug_end "$debug"
        return 0
    fi

    # Get the home directory of the current user
    homepath=$(
        getent passwd "${SUDO_USER:-$(whoami)}" | {
            IFS=':' read -r _ _ _ _ _ homedir _
            printf "%s" "$homedir"
        }
    )

    # Determine the log file location
    LOG_FILE="${LOG_FILE:-$homepath/$scriptname.log}"

    # Extract the log directory from the log file path
    log_dir="${LOG_FILE%/*}"

    # Check if the log directory exists and is writable
    debug_print "Checking if log directory '$log_dir' exists and is writable." "$debug"

    if [[ -d "$log_dir" && -w "$log_dir" ]]; then
        # Attempt to create the log file
        if ! touch "$LOG_FILE" &>/dev/null; then
            warn "Cannot create log file: $LOG_FILE"
            log_dir="/tmp"
        else
            # Change ownership of the log file if possible
            if [[ -n "${SUDO_USER:-}" && "${REQUIRE_SUDO:-true}" == "true" ]]; then
                chown "$SUDO_USER:$SUDO_USER" "$LOG_FILE" &>/dev/null || warn "Failed to set ownership to SUDO_USER: $SUDO_USER"
            else
                chown "$(whoami):$(whoami)" "$LOG_FILE" &>/dev/null || warn "Failed to set ownership to current user: $(whoami)"
            fi
        fi
    else
        log_dir="/tmp"
    fi

    # Fallback to /tmp if the directory is invalid
    if [[ "$log_dir" == "/tmp" ]]; then
        fallback_log="/tmp/$scriptname.log"
        LOG_FILE="$fallback_log"
        debug_print "Falling back to log file in /tmp: $LOG_FILE" "$debug"
        warn "Falling back to log file in /tmp: $LOG_FILE"
    fi

    # Attempt to create the log file in the fallback location
    if ! touch "$LOG_FILE" &>/dev/null; then
        debug_end "$debug"
        die 1 "Unable to create log file even in fallback location: $LOG_FILE"
    fi

    # Final debug message after successful log file setup
    debug_print "Log file successfully created at: $LOG_FILE" "$debug"

    readonly LOG_FILE
    export LOG_FILE

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Retrieve the terminal color code or attribute.
#
# @details This function uses `tput` to retrieve a terminal color code or
#          attribute (e.g., `sgr0` for reset, `bold` for bold text). If the
#          attribute is unsupported by the terminal, it returns an empty
#          string.
#
# @param $1 The terminal color code or attribute to retrieve.
#
# @return The corresponding terminal value or an empty string if unsupported.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
default_color() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    tput "$@" 2>/dev/null || printf "\n" # Fallback to an empty string on error

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Initialize terminal colors and text formatting.
# @details This function sets up variables for foreground colors, background
#          colors, and text formatting styles. It checks terminal capabilities
#          and provides fallback values for unsupported or non-interactive
#          environments.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @return None
#
# @example
# init_colors "debug"  # Initializes terminal colors with debug output.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
init_colors() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # General text attributes
    BOLD=$(default_color bold)
    DIM=$(default_color dim)
    SMSO=$(default_color smso)
    RMSO=$(default_color rmso)
    UNDERLINE=$(default_color smul)
    NO_UNDERLINE=$(default_color rmul)
    BLINK=$(default_color blink)
    NO_BLINK=$(default_color sgr0)
    ITALIC=$(default_color sitm)
    NO_ITALIC=$(default_color ritm)
    MOVE_UP=$(default_color cuu1)
    CLEAR_LINE=$(tput el)

    # Foreground colors
    FGBLK=$(default_color setaf 0)
    FGRED=$(default_color setaf 1)
    FGGRN=$(default_color setaf 2)
    FGYLW=$(default_color setaf 3)
    FGGLD=$(default_color setaf 220)
    FGBLU=$(default_color setaf 4)
    FGMAG=$(default_color setaf 5)
    FGCYN=$(default_color setaf 6)
    FGWHT=$(default_color setaf 7)
    FGRST=$(default_color setaf 9)
    FGRST=$(default_color setaf 39)

    # Background colors
    BGBLK=$(default_color setab 0)
    BGRED=$(default_color setab 1)
    BGGRN=$(default_color setab 2)
    BGYLW=$(default_color setab 3)
    BGGLD=$(default_color setab 220)
    [[ -z "$BGGLD" ]] && BGGLD="$BGYLW" # Fallback to yellow
    BGBLU=$(default_color setab 4)
    BGMAG=$(default_color setab 5)
    BGCYN=$(default_color setab 6)
    BGWHT=$(default_color setab 7)
    BGRST=$(default_color setab 9)

    # Reset all
    RESET=$(default_color sgr0)

    # Set variables as readonly
    # shellcheck disable=SC2034
    readonly RESET BOLD SMSO RMSO UNDERLINE NO_UNDERLINE DIM
    # shellcheck disable=SC2034
    readonly BLINK NO_BLINK ITALIC NO_ITALIC MOVE_UP CLEAR_LINE
    # shellcheck disable=SC2034
    readonly FGBLK FGRED FGGRN FGYLW FGBLU FGMAG FGCYN FGWHT FGRST FGGLD
    # shellcheck disable=SC2034
    readonly BGBLK BGRED BGGRN BGYLW BGBLU BGMAG BGCYN BGWHT BGRST

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Generate a separator string for terminal output.
# @details Creates heavy or light horizontal rules based on terminal width.
#          Optionally outputs debug information if the debug flag is set.
#
# @param $1 Type of rule: "heavy" or "light".
# @param $2 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @return The generated rule string or error message if an invalid type is
#         provided.
#
# @example
# generate_separator "heavy"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
generate_separator() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Normalize separator type to lowercase
    local type="${1,,}"
    local width="${COLUMNS:-80}"

    # Validate separator type
    if [[ "$type" != "heavy" && "$type" != "light" ]]; then
        warn "Invalid separator type: '$1'. Must be 'heavy' or 'light'."
        debug_end "$debug"
        return 1
    fi

    # Generate the separator based on type
    case "$type" in
    heavy)
        # Generate a heavy separator (═)
        printf '═%.0s' $(seq 1 "$width")
        ;;
    light)
        # Generate a light separator (─)
        printf '─%.0s' $(seq 1 "$width")
        ;;
    *)
        # Handle invalid separator type
        warn "Invalid separator type: $type"
        debug_end "$debug"
        return 1
        ;;
    esac

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Validate the logging configuration, including LOG_LEVEL.
# @details This function checks whether the current LOG_LEVEL is valid. If
#          LOG_LEVEL is not defined in the `LOG_PROPERTIES` associative array,
#          it defaults to "INFO" and displays a warning message.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global LOG_LEVEL The current logging verbosity level.
# @global LOG_PROPERTIES Associative array defining log level properties.
#
# @return void
#
# @example
# validate_log_level "debug"  # Enables debug output
# validate_log_level          # No debug output
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
validate_log_level() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Ensure LOG_LEVEL is a valid key in LOG_PROPERTIES
    if [[ -z "${LOG_PROPERTIES[$LOG_LEVEL]:-}" ]]; then
        # Print error message if LOG_LEVEL is invalid
        warn "Invalid LOG_LEVEL '$LOG_LEVEL'. Defaulting to 'INFO'."
        LOG_LEVEL="INFO" # Default to "INFO"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Sets up the logging environment for the script.
#
# @details
# This function initializes terminal colors, configures the logging
# environment, defines log properties, and validates both the log level and
# properties. It must be called before any logging-related functions.
#
# - Initializes terminal colors using `init_colors`.
# - Sets up the log file and directory using `init_log`.
# - Defines global log properties (`LOG_PROPERTIES`), including severity
#   levels, colors, and labels.
# - Validates the configured log level and ensures all required log properties
#   are defined.
#
# @note This function should be called once during script initialization.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @return void
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
setup_log() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Initialize terminal colors
    init_colors "$debug"

    # Initialize logging environment
    init_log "$debug"

    # Define log properties (severity, colors, and labels)
    declare -gA LOG_PROPERTIES=(
        ["DEBUG"]="DEBUG|${FGCYN}|0"
        ["INFO"]="INFO |${FGGRN}|1"
        ["WARNING"]="WARN |${FGGLD}|2"
        ["ERROR"]="ERROR|${FGMAG}|3"
        ["CRITICAL"]="CRIT |${FGRED}|4"
        ["EXTENDED"]="EXTD |${FGBLU}|0"
    )

    # Debug message for log properties initialization
    if [[ "$debug" == "debug" ]]; then
        printf "[DEBUG] Log properties initialized:\n" >&2

        # Iterate through LOG_PROPERTIES to print each level with its color
        for level in DEBUG INFO WARNING ERROR CRITICAL EXTENDED; do
            IFS="|" read -r custom_level color severity <<<"${LOG_PROPERTIES[$level]}"
            printf "[DEBUG] %s: %b%s%b\n" "$level" "$color" "$custom_level" "$RESET" >&2
        done
    fi

    # Validate the log level and log properties
    validate_log_level "$debug"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Toggle the USE_CONSOLE variable on or off.
# @details This function updates the global USE_CONSOLE variable to either
#          "true" (on) or "false" (off) based on the input argument. It also
#          prints debug messages when the debug flag is passed.
#
# @param $1 The desired state: "on" (to enable console logging) or "off" (to
#           disable console logging).
# @param $2 [Optional] Debug flag. Pass "debug" to enable debug output.
#
# @global USE_CONSOLE The flag to control console logging.
#
# @return 0 on success, 1 on invalid input.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
toggle_console_log() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local state="${1,,}" # Convert input to lowercase for consistency

    # Validate $state
    if [[ "$state" != "on" && "$state" != "off" ]]; then
        warn "Invalid state: '$state'. Must be 'on' or 'off'."
        debug_end "$debug"
        return 1
    fi

    # Process the desired state
    case "$state" in
    on)
        USE_CONSOLE="true"
        debug_print "Console logging enabled. USE_CONSOLE='$USE_CONSOLE', CONSOLE_STATE='$CONSOLE_STATE'" "$debug"
        ;;
    off)
        USE_CONSOLE="false"
        debug_print "Console logging disabled. USE_CONSOLE='$USE_CONSOLE', CONSOLE_STATE='$CONSOLE_STATE'" "$debug"
        ;;
    *)
        warn "Invalid argument for toggle_console_log: $state"
        debug_end "$debug"
        return 1
        ;;
    esac

    debug_end "$debug"
    return 0
}

############
### Get Project Parameters Functions
############

# -----------------------------------------------------------------------------
# @brief Retrieve the Git owner or organization name from the remote URL.
# @details Attempts to dynamically fetch the Git repository's organization
#          name from the current Git process. If not available, uses the global
#          `$REPO_ORG` if set. If neither is available, returns "unknown".
#          Provides debugging output when the "debug" argument is passed.
#
# @param $1 [Optional] Pass "debug" to enable verbose debugging output.
#
# @global REPO_ORG If set, uses this as the repository organization.
#
# @return Prints the organization name if available, otherwise "unknown".
# @retval 0 Success: the organization name is printed.
# @retval 1 Failure: prints an error message to standard error if the
#           organization cannot be determined.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
get_repo_org() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local repo_org
    local url

    # Attempt to retrieve organization dynamically from local Git environment
    url=$(git config --get remote.origin.url 2>/dev/null)
    if [[ -n "$url" ]]; then
        # Extract the owner or organization name from the Git URL
        repo_org=$(printf "%s" "$url" | sed -E 's#(git@|https://)([^:/]+)[:/]([^/]+)/.*#\3#')
        debug_print "Retrieved organization from local Git remote URL: $repo_org" "$debug"
    else
        warn "No remote origin URL retrieved."
    fi

    # If the organization is still empty, use $REPO_ORG (if set)
    if [[ -z "$repo_org" && -n "$REPO_ORG" ]]; then
        repo_org="$REPO_ORG"
        debug_print "Using global REPO_ORG: $repo_org" "$debug"
    fi

    # If organization is still empty, return "unknown"
    if [[ -z "$repo_org" ]]; then
        debug_print "Unable to determine organization. Returning 'unknown'." "$debug"
        repo_org="unknown"
    fi

    # Output the determined or fallback organization
    printf "%s\n" "$repo_org"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Retrieve the Git project name from the remote URL.
# @details Attempts to dynamically fetch the Git repository's name from the
#          current Git process. If not available, uses the global `$REPO_NAME`
#          if set. If neither is available, returns "unknown". Provides
#          debugging output when the "debug" argument is passed.
#
# @param $1 [Optional] Pass "debug" to enable verbose debugging output.
#
# @global REPO_NAME If set, uses this as the repository name.
#
# @return Prints the repository name if available, otherwise "unknown".
# @retval 0 Success: the repository name is printed.
# @retval 1 Failure: prints an error message to standard error if the
#         repository name cannot be determined.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
get_repo_name() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local repo_name="${REPO_NAME:-}" # Use existing $REPO_NAME if set
    local url

    # Attempt to retrieve repository name dynamically from Git
    if [[ -z "$repo_name" ]]; then
        url=$(git config --get remote.origin.url 2>/dev/null)
        if [[ -n "$url" ]]; then
            # Extract the repository name and remove the ".git" suffix if present
            repo_name="${url##*/}"        # Remove everything up to the last `/`
            repo_name="${repo_name%.git}" # Remove the `.git` suffix
            debug_print "Retrieved repository name from remote URL: $repo_name" "$debug"
        fi
    fi

    # Use "unknown" if no repository name could be determined
    if [[ -z "$repo_name" ]]; then
        debug_print "Unable to determine repository name. Returning 'unknown'." "$debug"
        repo_name="unknown"
    fi

    # Output the determined or fallback repository name
    printf "%s\n" "$repo_name"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Convert a Git repository name to title case.
# @details Replaces underscores and hyphens with spaces and converts words to
#          title case.  Provides debugging output when the "debug" argument is
#          passed.
#
# @param $1 The Git repository name (e.g., "my_repo-name").
# @param $2 [Optional] Pass "debug" to enable verbose debugging output.
#
# @return The repository name in title case (e.g., "My Repo Name").
# @retval 0 Success: the converted repository name is printed.
# @retval 1 Failure: prints an error message to standard error.
#
# @throws Exits with an error if the repository name is empty.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
repo_to_title_case() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local repo_name="${1:-}" # Input repository name
    local title_case         # Variable to hold the formatted name

    # Validate input
    if [[ -z "${repo_name:-}" ]]; then
        warn "Repository name cannot be empty."
        debug_end "$debug"
        return 1
    fi
    debug_print "Received repository name: $repo_name" "$debug"

    # Replace underscores and hyphens with spaces and convert to title case
    title_case=$(printf "%s" "$repo_name" | tr '_-' ' ' | awk '{for (i=1; i<=NF; i++) $i=toupper(substr($i,1,1)) substr($i,2)}1')

    local retval
    if [[ -n "${title_case:-}" ]]; then
        debug_print "Converted repository name to title case: $title_case" "$debug"
        printf "%s\n" "$title_case"
        retval=0
    else
        warn "Failed to convert repository name to title case."
        retval=1
    fi

    debug_end "$debug"
    return "$retval"
}

# -----------------------------------------------------------------------------
# @brief Retrieve the current Repo branch name or the branch this was detached
#        from.
# @details Attempts to dynamically fetch the branch name from the current Git
#          process. If not available, uses the global `$REPO_BRANCH` if set. If
#          neither is available, returns "unknown". Provides debugging output
#          when the "debug" argument is passed.
#
# @param $1 [Optional] Pass "debug" to enable verbose debugging output.
#
# @global REPO_BRANCH If set, uses this as the current Git branch name.
#
# @return Prints the branch name if available, otherwise "unknown".
# @retval 0 Success: the branch name is printed.
# @retval 1 Failure: prints an error message to standard error if the branch
#         name cannot be determined.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
get_repo_branch() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local branch="${REPO_BRANCH:-}" # Use existing $REPO_BRANCH if set
    local detached_from

    # Attempt to retrieve branch name dynamically from Git
    if [[ -z "$branch" ]]; then
        branch=$(git rev-parse --abbrev-ref HEAD 2>/dev/null)
        if [[ -n "$branch" && "$branch" != "HEAD" ]]; then
            debug_print "Retrieved branch name from Git: $branch" "$debug"
        elif [[ "$branch" == "HEAD" ]]; then
            # Handle detached HEAD state: attempt to determine the source
            detached_from=$(git reflog show --pretty='%gs' | grep -oE 'checkout: moving from [^ ]+' | head -n 1 | awk '{print $NF}')
            if [[ -n "$detached_from" ]]; then
                branch="$detached_from"
                debug_print "Detached HEAD state. Detached from branch: $branch" "$debug"
            else
                debug_print "Detached HEAD state. Cannot determine the source branch." "$debug"
                branch="unknown"
            fi
        fi
    fi

    # Use "unknown" if no branch name could be determined
    if [[ -z "$branch" ]]; then
        debug_print "Unable to determine Git branch. Returning 'unknown'." "$debug"
        branch="unknown"
    fi

    # Output the determined or fallback branch name
    printf "%s\n" "$branch"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Get the most recent Git tag.
# @details Attempts to dynamically fetch the most recent Git tag from the
#          current Git process. If not available, uses the global `$GIT_TAG` if
#          set. If neither is available, returns "0.0.1". Provides debugging
#          output when the "debug" argument is passed.
#
# @param $1 [Optional] Pass "debug" to enable verbose debugging output.
#
# @global GIT_TAG If set, uses this as the most recent Git tag.
#
# @return Prints the tag name if available, otherwise "0.0.1".
# @retval 0 Success: the tag name is printed.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
get_last_tag() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local tag

    # Attempt to retrieve the tag dynamically from Git
    tag=$(git describe --tags --abbrev=0 2>/dev/null)
    if [[ -n "$tag" ]]; then
        debug_print "Retrieved tag from Git: $tag" "$debug"
    else
        debug_print "No tag obtained from local repo." "$debug"
        # Try using GIT_TAG if it is set
        tag="${GIT_TAG:-}"
        # Fall back to "0.0.1" if both the local tag and GIT_TAG are unset
        if [[ -z "$tag" ]]; then
            tag="0.0.1"
            debug_print "No local tag and GIT_TAG is unset. Using fallback: $tag" "$debug"
        else
            debug_print "Using pre-assigned GIT_TAG: $tag" "$debug"
        fi
    fi

    # Output the tag
    printf "%s\n" "$tag"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check if a tag follows semantic versioning.
# @details Validates if a given Git tag follows the semantic versioning format
#          (major.minor.patch). Provides debugging output when the "debug"
#          argument is passed.
#
# @param $1 The Git tag to validate.
# @param $2 [Optional] Pass "debug" to enable verbose debugging output.
#
# @return Prints "true" if the tag follows semantic versioning, otherwise
#         "false".
# @retval 0 Success: the validation result is printed.
# @retval 1 Failure: prints an error message to standard error if no tag is
#         provided.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
is_sem_ver() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local tag="${1:-}"

    # Validate input
    if [[ -z "${tag:-}" ]]; then
        warn "Tag cannot be empty."
        debug_end "$debug"
        return 1
    fi

    debug_print "Validating tag: $tag" "$debug"

    # Check if the tag follows the semantic versioning format
    if [[ "$tag" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        debug_print "Tag $tag follows semantic versioning." "$debug"
        printf "true\n"
    else
        debug_print "Tag $tag does not follow semantic versioning." "$debug"
        printf "false\n"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Get the number of commits since the last tag.
# @details Counts commits since the provided Git tag using `git rev-list`. If
#          no tag is found, defaults to `0` commits. Debug messages are sent
#          only to `stderr`.
#
# @param $1 The Git tag to count commits from.
# @param $2 [Optional] Pass "debug" to enable verbose debugging output.
#
# @return The number of commits since the tag, or 0 if the tag does not exist.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
get_num_commits() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local tag="${1:-}"

    if [[ -z "$tag" || "$tag" == "0.0.1" ]]; then
        debug_print "No valid tag provided. Assuming 0 commits." "$debug"
        printf "0\n"
        debug_end "$debug"
        return 1
    fi

    local commit_count
    commit_count=$(git rev-list --count "${tag}..HEAD" 2>/dev/null || echo 0)

    printf "%s\n" "$commit_count"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Get the short hash of the current Git commit.
# @details Retrieves the short hash of the current Git commit. Provides
#          debugging output when the "debug" argument is passed.
#
# @param $1 [Optional] Pass "debug" to enable verbose debugging output.
#
# @return Prints the short hash of the current Git commit.
# @retval 0 Success: the short hash is printed.
# @retval 1 Failure: prints an error message to standard error if unable to
#         retrieve the hash.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
get_short_hash() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local short_hash
    short_hash=$(git rev-parse --short HEAD 2>/dev/null)
    if [[ -z "$short_hash" ]]; then
        debug_print "No short hash available. Using 'unknown'." "$debug"
        short_hash="unknown"
    else
        debug_print "Short hash of the current commit: $short_hash." "$debug"
    fi

    printf "%s\n" "$short_hash"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check if there are uncommitted changes in the working directory.
# @details Checks for uncommitted changes in the current Git repository.
#          Provides debugging output when the "debug" argument is passed.
#
# @param $1 [Optional] Pass "debug" to enable verbose debugging output.
#
# @return Prints "true" if there are uncommitted changes, otherwise "false".
# @retval 0 Success: the dirty state is printed.
# @retval 1 Failure: prints an error message to standard error if unable to
#         determine the repository state.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
get_dirty() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local changes

    # Check for uncommitted changes in the repository
    changes=$(git status --porcelain 2>/dev/null)

    if [[ -n "${changes:-}" ]]; then
        printf "true\n"
    else

        printf "false\n"
    fi

    if [[ -n "$changes" ]]; then
        debug_print "Changes detected." "$debug"
    else
        debug_print "No changes detected." "$debug"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Generate a version string based on the state of the Git repository.
# @details Constructs a semantic version string using the most recent Git tag,
#          current branch name, number of commits since the tag, the short hash
#          of the latest commit, and whether the working directory is dirty.
#          Provides debugging output when the "debug" argument is passed.
#
# @param $1 [Optional] Pass "debug" to enable verbose debugging output.
#
# @return Prints the generated semantic version string.
# @retval 0 Success: the semantic version string is printed.
# @retval 1 Failure: prints an error message to standard error if any required
#         Git information cannot be determined.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
get_sem_ver() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local tag branch_name num_commits short_hash dirty version_string

    # Retrieve the most recent tag
    tag=$(get_last_tag "$debug")
    debug_print "Received tag: $tag from get_last_tag()." "$debug"
    if [[ -z "$tag" || "$tag" == "0.0.1" ]]; then
        debug_print "No semantic version tag found (or version is 0.0.1). Using script version: $GIT_TAG" "$debug"
        version_string="$GIT_TAG"
    else
        version_string="$tag"
    fi

    # Append branch name
    branch_name=$(get_repo_branch "$debug")
    version_string="$version_string-$branch_name"
    debug_print "Appended branch name to version: $branch_name" "$debug"

    # Append number of commits since the last tag
    num_commits=$(get_num_commits "$tag" "$debug")
    if [[ "$num_commits" -gt 0 ]]; then
        version_string="$version_string+$num_commits"
        debug_print "Appended commit count '$num_commits' to version." "$debug"
    fi

    # Append short hash of the current commit
    short_hash=$(get_short_hash "$debug")
    version_string="$version_string.$short_hash"
    debug_print "Appended short hash '$short_hash' to version." "$debug"

    # Check if the repository is dirty
    dirty=$(get_dirty "$debug")
    if [[ "$dirty" == "true" ]]; then
        version_string="$version_string-dirty"
        debug_print "Repository is dirty. Appended '-dirty' to version." "$debug"
    fi

    printf "%s\n" "$version_string"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Retrieves and sets repository-related project parameters.
# @details This function initializes and exports key repository variables
#          based on the execution context. If the script is running inside
#          a GitHub repository, it gathers metadata such as organization,
#          repository name, branch, and tags. Otherwise, it configures remote
#          URLs for accessing the repository.
#
# @global REPO_ORG The GitHub organization or user associated with the repository.
# @global REPO_NAME The name of the GitHub repository.
# @global REPO_BRANCH The active branch of the repository.
# @global GIT_TAG The latest Git tag (release) for the repository.
# @global SEM_VER The semantic version retrieved from the latest Git tag.
# @global LOCAL_REPO_DIR The local root directory of the repository.
# @global LOCAL_SYSTEMD_DIR The path to the local systemd configuration directory.
# @global LOCAL_CONFIG_DIR The path to the local configuration directory.
# @global LOCAL_WWW_DIR The path to the local web assets directory.
# @global GIT_RAW The raw GitHub content URL for downloading repository files.
# @global GIT_API The GitHub API URL for accessing repository information.
# @global GIT_CLONE The GitHub clone URL for checking out the repository.
#
# @param $1 Debug mode flag (optional).
#
# @throws Terminates the script if repository metadata cannot be retrieved.
# @throws Terminates the script if required directories do not exist.
#
# @return 0 on success, 1 on failure.
#
# @example
# get_proj_params "$debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
get_proj_params() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if [[ "$IS_REPO" == "true" ]]; then
        debug_print "Configuring local mode with GitHub repository context." "$debug"

        # Retrieve repository details
        local retval=0 # Initialize return value

        REPO_ORG=$(get_repo_org "${debug}")
        retval=$((retval | $?))

        REPO_NAME=$(get_repo_name "${debug}")
        retval=$((retval | $?))

        REPO_BRANCH=$(get_repo_branch "${debug}")
        retval=$((retval | $?))

        GIT_TAG=$(get_last_tag "${debug}")
        retval=$((retval | $?))

        SEM_VER=$(get_sem_ver "${debug}")
        retval=$((retval | $?))

        if [[ $retval -ne 0 ]]; then
            debug_end "$debug"
            die 1 "Failed to retrieve repository parameters."
        fi

        # Get the root directory of the repository
        LOCAL_REPO_DIR=$(git rev-parse --show-toplevel 2>/dev/null)
        if [[ -z "${LOCAL_REPO_DIR:-}" ]]; then
            debug_end "$debug"
            die 1 "Not inside a valid Git repository. Ensure the repository is properly initialized."
        fi

        # Set local script paths based on repository structure
        LOCAL_WWW_DIR="$LOCAL_REPO_DIR/$UI_REPO_DIR/data"
        if [[ ! -d "${LOCAL_WWW_DIR:-}" ]]; then
            debug_end "$debug"
            die 1 "HTML source directory does not exist."
        fi

        LOCAL_CONFIG_DIR="$LOCAL_REPO_DIR/config"
        if [[ ! -d "${LOCAL_CONFIG_DIR:-}" ]]; then
            debug_end "$debug"
            die 1 "Configuration source directory does not exist."
        fi

        LOCAL_SYSTEMD_DIR="$LOCAL_REPO_DIR/systemd"
        if [[ ! -d "${LOCAL_SYSTEMD_DIR:-}" ]]; then
            debug_end "$debug"
            die 1 "systemd source directory does not exist."
        fi

        LOCAL_SOURCE_DIR="${LOCAL_REPO_DIR}/src"
        if [[ ! -d "${LOCAL_SOURCE_DIR:-}" ]]; then
            debug_end "$debug"
            die 1 "Source files directory does not exist."
        fi

        GIT_RAW="$GIT_RAW_BASE/$REPO_ORG/$REPO_NAME"
        GIT_API="$GIT_API_BASE/$REPO_ORG/$REPO_NAME"
        GIT_CLONE="$GIT_CLONE_BASE/$REPO_ORG/$REPO_NAME"

    else
        # Configure remote access URLs
        debug_print "Configuring remote mode." "$debug"
        if [[ -z "${REPO_ORG:-}" || -z "${REPO_NAME:-}" || -z "${REPO_BRANCH:-}" || -z "${GIT_TAG:-}" ]]; then
            debug_end "$debug"
            die 1 "Remote mode requires Git data to be set."
        fi

        SEM_VER="${SEM_VER:-0.0.1}"
        LOCAL_REPO_DIR="$USER_HOME/$REPO_NAME"
        LOCAL_SOURCE_DIR="${LOCAL_REPO_DIR}/src"
        LOCAL_SYSTEMD_DIR="${LOCAL_REPO_DIR}/systemd"
        LOCAL_CONFIG_DIR="${LOCAL_REPO_DIR}/config"
        LOCAL_WWW_DIR="${LOCAL_REPO_DIR}/${UI_REPO_DIR}/data"
        GIT_RAW="$GIT_RAW_BASE/$REPO_ORG/$REPO_NAME"
        GIT_API="$GIT_API_BASE/$REPO_ORG/$REPO_NAME"
        GIT_CLONE="$GIT_CLONE_BASE/$REPO_ORG/$REPO_NAME"
    fi

    # Export global variables for further use
    export REPO_ORG
    export REPO_NAME
    export REPO_BRANCH
    export GIT_TAG
    export SEM_VER
    export LOCAL_REPO_DIR
    export LOCAL_SOURCE_DIR
    export LOCAL_SYSTEMD_DIR
    export LOCAL_CONFIG_DIR
    export LOCAL_WWW_DIR
    export GIT_RAW
    export GIT_API
    export GIT_CLONE

    # Debug output for exported variables
    debug_print "Exported REPO_ORG: $REPO_ORG" "$debug"
    debug_print "Exported REPO_NAME: $REPO_NAME" "$debug"
    debug_print "Exported REPO_BRANCH: $REPO_BRANCH" "$debug"
    debug_print "Exported GIT_TAG: $GIT_TAG" "$debug"
    debug_print "Exported SEM_VER: $SEM_VER" "$debug"
    debug_print "Exported LOCAL_REPO_DIR: $LOCAL_REPO_DIR" "$debug"
    debug_print "Exported LOCAL_SOURCE_DIR: $LOCAL_SOURCE_DIR" "$debug"
    debug_print "Exported LOCAL_SYSTEMD_DIR: $LOCAL_SYSTEMD_DIR" "$debug"
    debug_print "Exported LOCAL_CONFIG_DIR: $LOCAL_CONFIG_DIR" "$debug"
    debug_print "Exported LOCAL_WWW_DIR: $LOCAL_WWW_DIR" "$debug"
    debug_print "Exported GIT_RAW: $GIT_RAW" "$debug"
    debug_print "Exported GIT_API: $GIT_API" "$debug"
    debug_print "Exported GIT_CLONE: $GIT_CLONE" "$debug"

    debug_end "$debug"
    return 0
}

############
### Git Functions
############

# -----------------------------------------------------------------------------
# @brief Clones a GitHub repository to the specified local destination.
# @details This function clones the repository from the provided Git URL to the
#          specified local destination directory.
#
# @global GIT_CLONE The base URL for cloning the GitHub repository.
# @global USER_HOME The home directory of the user, used as the base for
#         storing files.
# @global REPO_NAME The name of the repository to clone.
#
# @throws Logs an error and returns non-zero if the repository cloning fails.
#
# @return None. Clones the repository into the local destination.
#
# @example
# git_clone
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
git_clone() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local dest_root retval
    dest_root="$LOCAL_REPO_DIR"
    retval=0

    # Return if we are in the repo
    if [[ -d "$dest_root" && "${IS_REPO}" ]]; then
        debug_end "$debug"
        return 0
    fi

    debug_print "Ensuring destination directory does not exist: '$dest_root'" "$debug"
    if [[ -d "$dest_root" ]]; then
        logI "Destination directory already exists: '$dest_root'" "$debug"
        debug_end "$debug"
        return 1
    fi

    exec_command "Clone repository '$GIT_CLONE'" \
        runuser -u "$SUDO_USER" -- git clone -b "$REPO_BRANCH" \
            "$GIT_CLONE" "$dest_root" \
        "$debug" || {
        warn "Failed to clone repository from '$GIT_CLONE' to '$dest_root'"
        debug_end "$debug"
        return 1
    }

    debug_end "$debug"
    return "$retval"
}

# -----------------------------------------------------------------------------
# @brief Capture one stable source version before installation creates any
#        temporary files inside the checkout.
# @details Reinstall configuration merging temporarily creates a generated INI
#          below the repository root. Later version rendering must use this
#          pre-mutation value so identical installs produce identical artifacts.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
capture_install_sem_ver() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if [[ "$BINARY_SOURCE" != "build" ]]; then
        [[ -n "$BINARY_VERSION" ]] || { logE "Precompiled version was not validated."; return 1; }
        SEM_VER="$BINARY_VERSION"
        export SEM_VER
        return 0
    fi
    local captured_version=""

    captured_version=$(cd "$LOCAL_REPO_DIR" && get_sem_ver "$debug") || {
        logE "Unable to capture the installation source version."
        debug_end "$debug"
        return 1
    }
    if [[ -z "$captured_version" ]]; then
        logE "Installation source version is empty."
        debug_end "$debug"
        return 1
    fi

    SEM_VER="$captured_version"
    export SEM_VER
    debug_print "Captured stable installation version: $SEM_VER" "$debug"
    debug_end "$debug"
    return 0
}

############
### Common Script Functions
############

# -----------------------------------------------------------------------------
# @brief Sets the system timezone interactively or logs if already set.
# @details If the current timezone is not GMT or BST, logs the current date and
#          time, and exits. Otherwise, prompts the user to confirm or
#          reconfigure the timezone. If the debug flag is passed, additional
#          information about the process is logged.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable detailed output.
#
# @global TERSE Indicates terse mode (skips interactive messages).
#
# @return None Logs the current timezone or adjusts it if necessary.
#
# @example
# set_time debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
set_time() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local need_set=false
    local current_date tz yn

    # Get the current date and time
    current_date="$(date)"
    tz="$(date +%Z)"

    # Log and return if the timezone is not GMT or BST
    if [[ "$tz" != "GMT" && "$tz" != "BST" && "$tz" != "UTC" ]]; then
        need_set=true
        debug_print "Timezone '$tz' is neither GMT, BST, nor UTC" "$debug"
        debug_end "$debug"
        return 0
    fi

    # Check if the script is in terse mode
    if [[ "$TERSE" == "true" && "$need_set" == "true" ]]; then
        logW "Timezone detected as $tz, which may need to be updated."
        debug_end "$debug"
        return 1
    else
        logI "Timezone detected as $tz."
    fi

    # Inform the user about the current date and time
    logI "Timezone detected as $tz, which may need to be updated."

    if [[ "$DRY_RUN" == "true" ]]; then
        logI "Dry run: would confirm or reconfigure the system timezone."
        debug_end "$debug"
        return 0
    fi

    # Prompt for confirmation or reconfiguration
    while true; do
        read -rp "Is this correct? [y/N]: " yn </dev/tty
        case "$yn" in
        [Yy]*)
            logI "Timezone confirmed on $current_date"
            debug_print "Timezone confirmed: $current_date" "$debug"
            break
            ;;
        [Nn]* | *)
            dpkg-reconfigure tzdata
            logI "Timezone reconfigured on $current_date"
            debug_print "Timezone reconfigured: $current_date" "$debug"
            break
            ;;
        esac
    done

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Execute a new shell operation (departs this script).
# @details Executes or simulates a shell command based on the DRY_RUN flag.
#          Supports optional debugging to trace the execution process.
#
# @param $1 Name of the operation or process (for reference in logs).
# @param $2 The shell command to execute.
# @param $3 Optional debug flag ("debug" to enable debug output).
#
# @global FUNCNAME Used to fetch the current and caller function names.
# @global BASH_LINENO Used to fetch the calling line number.
# @global DRY_RUN When set, simulates command execution instead of running it.
#
# @throws Exits with a non-zero status if the command execution fails.
#
# @return None.
#
# @example
# DRY_RUN=true exec_new_shell "ListFiles" "ls -l" "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
exec_new_shell() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local exec_name="${1:-Unnamed Operation}"
    local exec_process="${2:-true}"

    # Debug information
    debug_print "exec_name: $exec_name" "$debug"
    debug_print " exec_process: $exec_process" "$debug"

    # Simulate command execution if DRY_RUN is enabled
    if [[ -n "$DRY_RUN" ]]; then
        printf "[✔] Simulating: '%s'.\n" "$exec_process"
        debug_end "$debug"
        exit_script 0 "$debug"
    fi

    # Validate the command
    if [[ "$exec_process" == "true" || "$exec_process" == "" ]]; then
        printf "[✔] Running: '%s'.\n" "$exec_process"
        debug_end "$debug"
        exec true
    elif ! command -v "${exec_process%% *}" >/dev/null 2>&1; then
        warn "'$exec_process' is not a valid command or executable."
        debug_end "$debug"
        die 1 "Invalid command: '$exec_process'"
    else
        # Execute the actual command
        printf "[✔] Running: '%s'.\n" "$exec_process"
        debug_print "Executing command: '$exec_process' in function '$func_name()' at line ${LINENO}." "$debug"
        exec $exec_process || die 1 "Command '${exec_process}' failed"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Installs or upgrades all packages in the APT_PACKAGES list.
# @details Updates the package list and resolves broken dependencies before
#          proceeding. Accumulates errors for each failed package and logs a
#          summary at the end. Skips execution if the APT_PACKAGES array is
#          empty.
#
# @param $1 [Optional] Debug flag. Pass "debug" to enable detailed output.
#
# @global APT_PACKAGES List of packages to install or upgrade.
#
# @return 0 if all operations succeed, 1 if any operation fails.
#
# @example
# handle_apt_packages debug
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
handle_apt_packages() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local error_count=0
    local runtime_pkg=""
    local packages_to_install=()

    # Update package list and fix broken installs
    if ! exec_command "Update local package index" apt-get update "$debug"; then
        warn "Failed to update package list."
        ((error_count++))
    fi
    if ! exec_command \
        "Fix broken or incomplete package installations" \
        apt-get install -f -y "$debug"; then
        warn "Failed to fix broken installs."
        ((error_count++))
    fi

    if [[ "$BINARY_SOURCE" == "build" ]]; then
        runtime_pkg="$(resolve_libgpiod_runtime_pkg "$debug")"
        if [[ -z "$runtime_pkg" ]]; then
            warn "Failed to resolve required libgpiod runtime package."
            return 1
        fi
    fi

    # Build the final platform-aware install list. RP1 kernel-development
    # packages are absent unless provider installation is selected.
    if ! resolve_apt_package_list "$runtime_pkg"; then
        warn "Failed to resolve the platform-specific APT package list."
        debug_end "$debug"
        return 1
    fi
    packages_to_install=( "${RESOLVED_APT_PACKAGES[@]}" )

    # One APT transaction avoids reparsing the package index for every package
    # on low-memory Pis. Do not remove previously installed development tools.
    if ! exec_command "Install required packages" apt-get install -y \
        "${packages_to_install[@]}" "$debug"; then
        warn "Failed to install the required package set."
        ((error_count++))
    fi

    # Log summary of errors
    if ((error_count > 0)); then
        warn "APT package handling completed with $error_count errors."
        debug_print "APT package handling completed with $error_count errors." \
            "$debug"
    else
        debug_print "APT package handling completed successfully." "$debug"
    fi

    debug_end "$debug"
    return "$error_count"
}

# shellcheck disable=SC2317
run_support_bundle_age_dependency_validation() {
    # shellcheck source=scripts/support_bundle_age_dependency.sh
    source "${LOCAL_REPO_DIR}/scripts/support_bundle_age_dependency.sh" &&
        support_bundle_validate_age_dependency
}

# shellcheck disable=SC2317
validate_support_bundle_age_dependency() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if ! exec_command "Validate support-bundle encryption tools" \
        run_support_bundle_age_dependency_validation "$debug"; then
        logE "Required support-bundle encryption tools are unavailable or unsafe."
        debug_print "Support-bundle encryption validation status: ${SUPPORT_BUNDLE_AGE_DEPENDENCY_ERROR:-helper_unavailable}." "$debug"
        debug_end "$debug"
        return 1
    fi
    if [[ "$DRY_RUN" == "true" ]]; then
        debug_print "Support-bundle encryption validation planned but not executed." "$debug"
    else
        debug_print "Validated fixed support-bundle encryption tools." "$debug"
    fi
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Resolve the libgpiod runtime package required by libgpiod-dev.
# @details Ensures libgpiod-dev has a non-(none) candidate in the configured
#          APT repos, then extracts the SONAME runtime dependency (libgpiod2,
#          libgpiod3, etc.) from APT metadata. Validates that the resolved
#          runtime package exists in the repo.
#
#          Uses POSIX character classes for awk to remain compatible with mawk.
#          Uses apt-cache show to extract the dependency, which is less likely
#          to change format than apt-cache depends output.
#
# @param $1 [Optional] Debug flag.
#
# @return 0 on success, 1 on failure.
# @stdout Prints the resolved runtime package name (e.g., libgpiod3).
# -----------------------------------------------------------------------------
resolve_libgpiod_runtime_pkg() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local dev_candidate=""
    local runtime_pkg=""
    local runtime_candidate=""

    # Ensure libgpiod-dev is available (non-(none) candidate).
    dev_candidate="$(apt-cache policy libgpiod-dev 2>/dev/null \
        | awk '/^[[:space:]]*Candidate:[[:space:]]*/ {print $2; exit}')"
    if [[ -z "$dev_candidate" || "$dev_candidate" == "(none)" ]]; then
        warn "libgpiod-dev has no installable candidate in configured APT repos."
        debug_print "apt-cache policy libgpiod-dev candidate is empty or (none)." \
            "$debug"
        debug_end "$debug"
        return 1
    fi

    debug_print "libgpiod-dev candidate version is '$dev_candidate'." "$debug"

    # Extract the SONAME runtime dependency for libgpiod-dev from package metadata.
    runtime_pkg="$(apt-cache show libgpiod-dev 2>/dev/null \
        | awk -F'[:, ]+' '/^Depends:/ {
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^libgpiod[0-9]+$/) {
                    print $i
                    exit
                }
            }
        }')"
    if [[ -z "$runtime_pkg" ]]; then
        warn "Unable to resolve libgpiod runtime dependency from libgpiod-dev."
        debug_print "apt-cache show did not expose a libgpiodN dependency." \
            "$debug"
        debug_end "$debug"
        return 1
    fi

    # Ensure the resolved runtime package is also available (non-(none) candidate).
    runtime_candidate="$(apt-cache policy "$runtime_pkg" 2>/dev/null \
        | awk '/^[[:space:]]*Candidate:[[:space:]]*/ {print $2; exit}')"
    if [[ -z "$runtime_candidate" || "$runtime_candidate" == "(none)" ]]; then
        warn "Resolved runtime package '$runtime_pkg' has no installable candidate."
        debug_print "apt-cache policy '$runtime_pkg' candidate is empty or (none)." \
            "$debug"
        debug_end "$debug"
        return 1
    fi

    debug_print "Resolved libgpiod runtime package is '$runtime_pkg'." "$debug"
    debug_print "Runtime candidate version is '$runtime_candidate'." "$debug"

    printf "%s\n" "$runtime_pkg"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Handles script exit operations and logs the exit message.
# @details This function is designed to handle script exit operations by logging
#          the exit message along with the status code, function name, and line
#          number where the exit occurred. The function also supports an optional
#          message and exit status, with default values provided if not supplied.
#          After logging the exit message, the script will terminate with the
#          specified exit status.
#
# @param $1 [optional] Exit status code (default is 1 if not provided).
# @param $2 [optional] Message to display upon exit (default is "Exiting
#           script.").
#
# @return None.
#
# @example
# exit_script 0 "Completed successfully"
# exit_script 1 "An error occurred"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
exit_script() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Local variables
    local exit_status="${1:-}"            # First parameter as exit status
    local message="${2:-Exiting script.}" # Main error message wit default
    local details                         # Additional details
    local lineno="${BASH_LINENO[0]}"      # Line number of calling line
    lineno=$(pad_with_spaces "$lineno")   # Pad line number with spaces
    local caller_func="${FUNCNAME[1]}"    # Calling function name

    # Determine exit status if not numeric
    if ! [[ "$exit_status" =~ ^[0-9]+$ ]]; then
        exit_status=1
    else
        shift # Remove the exit_status from the arguments
    fi

    # Remove trailing dot if needed
    message=$(remove_dot "$message")
    # Log the provided or default message
    printf "[EXIT ] '%s' from %s:%d status (%d).\n" "$message" "$caller_func" "$lineno" "$exit_status"

    debug_end "$debug"
    exit "$exit_status" # Exit with the provided status
}

############
### Arguments Functions
############

# -----------------------------------------------------------------------------
# @brief List of word arguments.
# @details Each entry in the list corresponds to a word argument and contains
#          the argument name, the associated function, a brief description,
#          and a flag indicating whether the function should exit after
#          processing the argument.
#
# @var ARGUMENTS_LIST
# @brief List of word arguments.
# @details The list holds the word arguments, their corresponding functions,
#          descriptions, and exit flags. Each word argument triggers a
#          specific function when encountered on the command line.
# -----------------------------------------------------------------------------
ARGUMENTS_LIST=(
    "debug true Executes with verbose debug information 0"
)

# -----------------------------------------------------------------------------
# @brief List of flagged arguments.
# @details Each entry in the list corresponds to a flagged argument, containing
#          the flag(s), a complex flag indicating if a secondary argument is
#          required, the associated function, a description, and an exit flag
#          indicating whether the function should terminate after processing.
#
# @var OPTIONS_LIST
# @brief List of flagged arguments.
# @details This list holds the flags (which may include multiple pipe-delimited
#          options), the associated function to call, whether a secondary
#          argument is required, and whether the function should exit after
#          processing.
# -----------------------------------------------------------------------------
OPTIONS_LIST=(
    "-h|--help 0 usage Show these instructions 1"
    "-v|--version 0 print_version Display $WSPR_SERVICE version 1"
    "--no-web 0 set_no_web Disable web UI installation and Apache integration (service will run with --no-web) 0"
    "--binary-source 1 set_binary_source Choose build (default), local, or release executable 0"
    "--binary-path 1 set_binary_path Use a local executable (default: invoking user's ~/wsprrypi) 0"
    "--release-tag 1 set_binary_release_tag Select the release tag for --binary-source release 0"
    "--release 0 set_release_build Build as release regardless of branch 0"
    "--fail-on-ui-modifications 0 set_fail_on_ui_modifications Refuse to replace a modified or unknown installed UI 0"
)

# -----------------------------------------------------------------------------
# @brief Processes command-line arguments.
# @details This function processes both word arguments (defined in
#          `ARGUMENTS_LIST`) and flagged options (defined in `OPTIONS_LIST`).
#          It handles complex flags that require a following argument, and
#          calls the associated functions for each valid argument. If an
#          invalid argument is encountered, it will trigger the `usage()`
#          function to display help instructions.
#
# @param $@ [optional] Command-line arguments passed to the function.
# @global ARGUMENTS_LIST List of valid word arguments and their associated
#                        functions.
# @global OPTIONS_LIST List of valid flagged options and their associated
#                      functions.
# @global debug_flag Optional debug flag to enable debugging information.
#
# @return 0 on success, or the status code of the last executed command.
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# @brief Force a release build from the command line.
# @details Sets `BUILD_TYPE_OVERRIDE` to `RELEASE`, re-resolves the effective
#          build settings, and updates the default log level when the user did
#          not explicitly provide `LOG_LEVEL` in the environment.
#
# @global BUILD_TYPE_OVERRIDE Optional explicit build override.
# @global LOG_LEVEL Current logging level.
# @global LOG_LEVEL_SET_FROM_ENV Tracks whether LOG_LEVEL came from the
#                                environment.
# @global WSPR_BUILD_TYPE The resolved build type.
#
# @param $@ Optional debug flag.
#
# @return 0 on success.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2329
set_release_build() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    BUILD_TYPE_OVERRIDE="RELEASE"
    resolve_build_settings "$debug"
    if [[ "${LOG_LEVEL_SET_FROM_ENV}" != "true" ]]; then
        LOG_LEVEL="INFO"
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Disable web UI installation and runtime.
# @details Sets the installer state so web assets and Apache integration are
#          skipped and the installed service starts with `--no-web`.
#
# @param $@ Optional debug flag.
#
# @return 0 on success.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2329
set_no_web() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    NO_WEB="true"

    debug_end "$debug"
    return 0
}

# shellcheck disable=SC2329
set_fail_on_ui_modifications() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    FAIL_ON_UI_MODIFICATIONS="true"
    debug_end "$debug"
    return 0
}

# shellcheck disable=SC2317
process_args() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    local retval=0
    local args=("$@")
    local invalid_argument=false

    # Loop through all the arguments passed to the function.
    while ((${#args[@]} > 0)); do
        local current_arg="${args[0]}"
        local processed_argument=false

        # Skip empty arguments.
        if [[ -z "${current_arg}" ]]; then
            args=("${args[@]:1}") # Remove the blank argument and continue
            continue
        fi

        # Process flagged options (starting with "-").
        if [[ "${current_arg:0:1}" == "-" ]]; then
            # Loop through all flagged options (OPTIONS_LIST)
            for entry in "${OPTIONS_LIST[@]}"; do
                local flag
                local complex_flag
                local function_name
                local description
                local exit_flag
                flag=$(echo "$entry" | cut -d' ' -f1)
                complex_flag=$(echo "$entry" | cut -d' ' -f2)
                function_name=$(echo "$entry" | cut -d' ' -f3)
                description=$(echo "$entry" | cut -d' ' -f4- | rev | cut -d' ' -f2- | rev)
                exit_flag=$(echo "$entry" | awk '{print $NF}')

                # Split flags and check if current_arg matches.
                IFS='|' read -ra flag_parts <<<"$flag" # Split the flag by "|"
                for part in "${flag_parts[@]}"; do
                    part=$(echo "$part" | xargs) # Trim spaces

                    # Check if the current argument matches any of the flags
                    if [[ "$current_arg" == "$part" ]]; then
                        # If it's a complex flag, we expect a following argument
                        if ((complex_flag == 1)); then
                            local next_arg
                            if [[ ${#args[@]} -ge 2 ]]; then
                                next_arg="${args[1]}"
                            else
                                die 1 "Error: Missing argument for flag '$part'."
                            fi

                            # Call the function with the next argument as a parameter
                            $function_name "$next_arg" "$debug"
                            retval="$?"
                            [[ "$retval" -eq 0 ]] || return "$retval"

                            # Remove the processed flag and its argument
                            args=("${args[@]:2}")
                            processed_argument=true
                        else
                            # Call the function with no arguments
                            $function_name
                            retval="$?"
                            [[ "$retval" -eq 0 ]] || return "$retval"
                            # Remove the processed flag
                            args=("${args[@]:1}")
                            processed_argument=true
                        fi

                        # Exit if exit_flag is set
                        if ((exit_flag == 1)); then
                            debug_end "$debug"
                            printf "\n"
                            exit
                        fi
                        continue
                    fi
                done
            done
        elif [[ -n "${current_arg}" ]]; then
            # Process single-word arguments from ARGUMENTS_LIST.
            for entry in "${ARGUMENTS_LIST[@]}"; do
                local word
                local function_name
                local description
                local exit_flag
                word=$(echo "$entry" | cut -d' ' -f1)
                function_name=$(echo "$entry" | cut -d' ' -f2)
                description=$(echo "$entry" | cut -d' ' -f3- | rev | cut -d' ' -f2- | rev)
                exit_flag=$(echo "$entry" | awk '{print $NF}')

                # Check if the current argument matches the word argument
                if [[ "$current_arg" == "$word" ]]; then
                    # Call the associated function
                    $function_name "$debug"
                    retval="$?"

                    # Exit if exit_flag is set
                    if ((exit_flag == 1)); then
                        debug_end "$debug"
                        exit_script "$retval"
                    fi

                    # Remove the processed argument from args
                    args=("${args[@]:1}")
                    processed_argument=true
                    break
                fi
            done
        fi

        # Handle invalid arguments by setting the flag.
        if [[ "$processed_argument" != true ]]; then
            args=("${args[@]:1}")
            invalid_argument=true
            continue
        fi
    done

    # If any invalid argument is found, show usage instructions.
    if [[ "$invalid_argument" == true ]]; then
        usage stderr
    fi

    debug_end "$debug"
    return "$retval"
}

# -----------------------------------------------------------------------------
# @brief Prints usage information for the script.
# @details This function prints out the usage instructions for the script,
#          including the script name, command-line options, and their
#          descriptions. The usage of word arguments and flag arguments is
#          displayed separately. It also handles the optional inclusion of
#          the `sudo` command based on the `REQUIRE_SUDO` environment variable.
#          Additionally, it can direct output to either stdout or stderr based
#          on the second argument.
#
# @param $@ [optional] Command-line arguments passed to the function,
#                      typically used for the debug flag.
# @global REQUIRE_SUDO If set to "true", the script name will include "sudo"
#                      in the usage message.
# @global THIS_SCRIPT The script's name, used for logging.
#
# @return 0 on success.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
usage() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    local output_redirect="1" # Default to stdout (1)
    local args=()

    # Check for the "stderr" argument to redirect output to stderr.
    for arg in "$@"; do
        if [[ "$arg" == "stderr" ]]; then
            output_redirect="2" # Set to stderr (2)
            shift
            break # Exit the loop as soon as we find "stderr"
        fi
    done

    # Check if "sudo" should be appended to the script name
    local script_name
    [[ "${REQUIRE_SUDO:-}" == "true" ]] && script_name+="sudo "
    script_name+=" ./$THIS_SCRIPT"

    # Print the usage with the correct script name
    printf "Usage: %s [debug] <option1> [<option2> ...]\n\n" "$script_name" >&$output_redirect

    # Word Arguments section
    printf "Available Options\n\n" >&"$output_redirect"
    printf "Word Arguments:\n" >&"$output_redirect"

    local max_word_len=0
    # First pass to calculate the maximum lengths of the word arguments
    for entry in "${ARGUMENTS_LIST[@]}"; do
        local word
        word=$(echo "$entry" | cut -d' ' -f1)
        local word_len=${#word}
        if ((word_len > max_word_len)); then
            max_word_len=$word_len
        fi
    done

    # Second pass to print with padded formatting
    for entry in "${ARGUMENTS_LIST[@]}"; do
        local word
        word=$(echo "$entry" | cut -d' ' -f1)
        local description
        description=$(echo "$entry" | cut -d' ' -f3- | rev | cut -d' ' -f2- | rev)
        local exit_flag=$((1 - $(echo "$entry" | awk '{print $NF}'))) # Invert the value

        printf "  %$((max_word_len))s: %s\n" "$word" "$description" >&"$output_redirect"
    done
    printf "\n" >&"$output_redirect"

    # Flag Arguments section
    printf "Flag Arguments:\n" >&"$output_redirect"
    local max_flag_len=0
    for entry in "${OPTIONS_LIST[@]}"; do
        local flag
        flag=$(echo "$entry" | cut -d' ' -f1)
        local flag_len=${#flag}
        if ((flag_len > max_flag_len)); then
            max_flag_len=$flag_len
        fi
    done

    # Second pass to print with padded formatting for flag arguments
    for entry in "${OPTIONS_LIST[@]}"; do
        local flag
        flag=$(echo "$entry" | cut -d' ' -f1)
        local complex_flag
        complex_flag=$(echo "$entry" | cut -d' ' -f2)
        local description
        description=$(echo "$entry" | cut -d' ' -f4- | rev | cut -d' ' -f2- | rev)
        local exit_flag=$((1 - $(echo "$entry" | awk '{print $NF}'))) # Invert the value

        printf "  %$((max_flag_len))s: %s\n" "$(echo "$flag" | tr '|' ' ')" "$description" >&"$output_redirect"
    done

    debug_end "$debug"
    return 0
}

############
###  Wsprry Pi Cleanup Functions
############

# -----------------------------------------------------------------------------
# @brief Removes specified services from systemd if they exist and are enabled.
#
# @details This function attempts to stop the service only if it is running,
#          disable it if it is enabled, remove the service unit file (even if
#          the service was never installed), and always resets the failed state
#          for each service. The user will be asked for confirmation if any
#          unexpected dependencies are found. After all services are processed,
#          the systemd daemon is reloaded to apply the changes.
#
# @return None
#
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
remove_legacy_services() {
    local debug services service_name unit_dash unit_uscore name full dir unit_file
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    services=(
        wspr
        shutdown-button
        shutdown-watch
        shutdown_watch
        wspr_watch
    )

    for service_name in "${services[@]}"; do
        # derive both dash- and underscore-variants
        unit_dash="${service_name//_/-}"
        unit_uscore="${unit_dash//-/_}"

        # for each variant, stop if running, disable if enabled
        for name in "${unit_dash}" "${unit_uscore}"; do
            full="${name}.service"

            if systemctl is-active --quiet "$full" 2>/dev/null; then
                exec_command "Stop ${full}" \
                    systemctl stop "${full}" "$debug"
            fi

            if systemctl is-enabled --quiet "$full" 2>/dev/null; then
                exec_command "Disable ${full}" \
                    systemctl disable "${full}" "$debug"
            fi
        done

        # now rip out any on-disk unit files (both forms)
        for dir in /etc/systemd/system /lib/systemd/system /usr/lib/systemd/system; do
            for name in "${unit_dash}" "${unit_uscore}"; do
                unit_file="${dir}/${name}.service"
                if [ -f "$unit_file" ]; then
                    exec_command "Remove unit file ${unit_file}" \
                        rm -f -- "${unit_file}" "$debug"
                fi
            done
        done
    done

    # final cleanup
    exec_command "Reset systemd states" systemctl reset-failed "$debug"
    exec_command "Reload systemd daemon" systemctl daemon-reload "$debug"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Removes specified files and directories from the system.
#
# @details This function attempts to remove files and directories listed in
#          the predefined array or passed in as arguments. It checks whether
#          each file or directory exists, and confirms with the user before
#          removing items if unexpected dependencies or locations are detected.
#          All errors are suppressed and warnings are printed to the terminal.
#
# @return None
#
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
remove_legacy_files_and_dirs() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local files_and_dirs

    # Cleanup List
    files_and_dirs=(
        "/usr/local/bin/wspr"
        "/usr/local/bin/shutdown-button.py"
        "/usr/local/bin/shutdown-watch.py"
        "/usr/local/bin/shutdown_watch.py"
        "/usr/local/bin/wspr_watch.py"
        "/var/www/html/wspr/"
        "/var/log/wspr/"
        "/var/log/WsprryPi/"
        "/etc/logrotate.d/wspr"
        "/etc/logrotate.d/wsprrypi"
    )

    for item in "${files_and_dirs[@]}"; do
        if [[ -e $item ]]; then
            exec_command "Remove $item" rm -rf -- "${item}" "$debug"
        fi
    done

    debug_end "$debug"
    return 0
}

############
### Wsprry Pi Installer Functions
############

# -----------------------------------------------------------------------------
# @brief Initiates the script execution and handles user interaction.
# @details This function validates the action (`install` or `uninstall`),
#          displays an introductory message, and prompts the user for
#          confirmation before proceeding. If the script is running in
#          terse mode, the prompt is skipped.
#
# @global ACTION Specifies whether the function runs in 'install' or 'uninstall' mode.
# @global REPO_TITLE The display name of the repository.
# @global TERSE If set to "true", suppresses user prompts.
#
# @param $1 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if an invalid action is specified or the user cancels.
#
# @return Returns 0 if the script proceeds, 1 if the user quits or an invalid
#         action is specified.
#
# @example
#   start_script "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
start_script() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Validate action
    if [[ "$ACTION" != "install" && "$ACTION" != "uninstall" ]]; then
        debug_print "Invalid action: $ACTION. Must be 'install' or 'uninstall'." "$debug"
        logE "Invalid action specified: $ACTION. Use 'install' or 'uninstall'."
        debug_end "$debug"
        return 1
    fi

    # Adjust log and prompt messages based on action
    local action_message
    if [[ "$ACTION" == "install" ]]; then
        action_message="installation"
    else
        action_message="uninstallation"
    fi

    # Check terse mode
    if [[ "${TERSE:-false}" == "true" ]]; then
        logI "$REPO_TITLE $action_message beginning."
        debug_print "Skipping interactive message due to terse mode." "$debug"
        debug_end "$debug"
        return 0
    fi

    # Prompt user for input
    printf "\n"
    printf "Starting %s for: %s.\n" "$action_message" "$REPO_TITLE"
    printf "Press any key to continue or 'Q' to quit (continuing install in 10 seconds).\n"

    # Read a single key with a 10-second timeout
    local key
    if ! read -n 1 -sr -t 10 key </dev/tty; then
        key="" # Assign a default value on timeout
    fi
    printf "\n"

    # Handle user input
    case "${key}" in
    [Qq]) # Quit
        debug_print "Quit key pressed. Ending $action_message." "$debug"
        logI "$action_message canceled by user."
        debug_end "$debug"
        return 1
        ;;
    "") # Timeout or Enter
        debug_print "No key pressed, proceeding with $action_message." "$debug"
        ;;
    *) # Any other key
        debug_print "Key pressed: '$key'. Proceeding with $action_message." "$debug"
        ;;
    esac

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Read one numeric KiB value from a meminfo-format file.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
build_resource_meminfo_kb() {
    local path="${1:-}"
    local key="${2:-}"

    [[ -n "$path" && -n "$key" && -r "$path" ]] || return 1
    awk -v wanted="${key}:" '$1 == wanted { print $2; found = 1; exit }
        END { if (!found) exit 1 }' "$path"
}

# -----------------------------------------------------------------------------
# @brief Report free active swap as total, zram, file, partition, other, and
#        independently backed values (KiB).
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
build_resource_swap_free_kb() {
    local path="${1:-}"

    [[ -n "$path" && -r "$path" ]] || return 1
    awk '
        NR == 1 { next }
        NF >= 4 {
            available = $3 - $4
            if (available < 0) available = 0
            total += available
            if ($1 ~ /(^|\/)zram[0-9]+$/) zram += available
            else {
                independent += available
                if ($2 == "file") file += available
                else if ($2 == "partition") partition += available
                else other += available
            }
        }
        END {
            printf "%d %d %d %d %d %d\n", total + 0, zram + 0,
                file + 0, partition + 0, other + 0, independent + 0
        }
    ' "$path"
}

# -----------------------------------------------------------------------------
# @brief Return the required free non-zram swap for the supplied total RAM.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
required_non_zram_build_swap_kb() {
    local mem_total_kb="${1:-0}"

    if ((mem_total_kb < BUILD_RESOURCE_VERY_LOW_RAM_KB)); then
        printf '%s\n' "$BUILD_RESOURCE_VERY_LOW_RAM_SWAP_KB"
    elif ((mem_total_kb < BUILD_RESOURCE_LOW_RAM_KB)); then
        printf '%s\n' "$BUILD_RESOURCE_LOW_RAM_SWAP_KB"
    else
        printf '0\n'
    fi
}

# -----------------------------------------------------------------------------
# @brief Test whether this invocation's temporary swap is still active.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
temp_build_swap_is_active() {
    local path="${1:-}"
    [[ -n "$path" && -r "$BUILD_RESOURCE_SWAPS_PATH" ]] || return 1
    awk -v expected="$path" 'NR > 1 && $1 == expected { found = 1 }
        END { exit(found ? 0 : 1) }' "$BUILD_RESOURCE_SWAPS_PATH"
}

# -----------------------------------------------------------------------------
# @brief Disable and remove only temporary swap created by this invocation.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
cleanup_temp_build_swap() {
    local path="${TEMP_BUILD_SWAP_PATH:-}"

    if [[ "${TEMP_BUILD_SWAP_OWNED:-false}" != "true" || -z "$path" ]]; then
        return 0
    fi

    if [[ "${TEMP_BUILD_SWAP_ACTIVE:-false}" == "true" ]] ||
        temp_build_swap_is_active "$path"; then
        if ! exec_command "Disable temporary build swap" swapoff "$path"; then
            logE "Unable to disable installer-owned temporary build swap: $path"
            logE "The active file was preserved. Recover with: sudo swapoff '$path' && sudo rm -f '$path'"
            return 1
        fi
        TEMP_BUILD_SWAP_ACTIVE="false"
    fi

    if temp_build_swap_is_active "$path"; then
        logE "Temporary build swap still appears active after swapoff: $path"
        logE "The file was preserved; do not remove it until swapoff succeeds."
        return 1
    fi

    if [[ -e "$path" ]]; then
        exec_command "Remove temporary build swap file" rm -f -- "$path" || return 1
    fi
    if [[ -e "$path" ]]; then
        logE "Unable to remove installer-owned temporary build swap: $path"
        return 1
    fi

    if [[ "${TEMP_BUILD_SWAP_ROOT_OWNED:-false}" == "true" ]]; then
        rmdir -- "$BUILD_RESOURCE_SWAP_ROOT" 2>/dev/null || true
    fi

    logI "Removed installer-owned temporary build swap."
    TEMP_BUILD_SWAP_PATH=""
    TEMP_BUILD_SWAP_OWNED="false"
    TEMP_BUILD_SWAP_ROOT_OWNED="false"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Create enough invocation-owned disk swap to meet the reviewed gate.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
create_temp_build_swap() {
    local required_kb="${1:-0}"
    local existing_non_zram_kb="${2:-0}"
    local shortfall_kb
    local allocation_kb
    local required_disk_kb
    local disk_probe_path
    local disk_available_kb
    local directory_mode
    local directory_owner
    local tool

    if [[ "${TEMP_BUILD_SWAP_OWNED:-false}" == "true" ]]; then
        if temp_build_swap_is_active "${TEMP_BUILD_SWAP_PATH:-}"; then
            logI "Invocation-owned temporary build swap is already active."
            return 0
        fi
        logE "Refusing to replace tracked temporary build swap that is not active: ${TEMP_BUILD_SWAP_PATH:-unknown}"
        return 1
    fi

    shortfall_kb=$((required_kb - existing_non_zram_kb))
    ((shortfall_kb > 0)) || return 0
    allocation_kb=$((
        (shortfall_kb + BUILD_RESOURCE_SWAP_METADATA_ALLOWANCE_KB +
            BUILD_RESOURCE_SWAP_ALLOCATION_QUANTUM_KB - 1) /
        BUILD_RESOURCE_SWAP_ALLOCATION_QUANTUM_KB *
        BUILD_RESOURCE_SWAP_ALLOCATION_QUANTUM_KB
    ))
    required_disk_kb=$((allocation_kb + BUILD_RESOURCE_DISK_RESERVE_KB))

    disk_probe_path="$BUILD_RESOURCE_SWAP_ROOT"
    while [[ ! -e "$disk_probe_path" && "$disk_probe_path" != "/" ]]; do
        disk_probe_path=$(dirname "$disk_probe_path")
    done

    if [[ -n "$BUILD_RESOURCE_DISK_AVAILABLE_KB" ]]; then
        disk_available_kb="$BUILD_RESOURCE_DISK_AVAILABLE_KB"
    else
        disk_available_kb=$(df -Pk "$disk_probe_path" 2>/dev/null |
            awk 'NR == 2 { print $4 }')
    fi
    if [[ ! "$disk_available_kb" =~ ^[0-9]+$ ]]; then
        logE "Unable to determine free space for temporary build swap."
        return 1
    fi
    if ((disk_available_kb < required_disk_kb)); then
        logE "Insufficient disk space for temporary build swap."
        logE "Need ${allocation_kb} KiB plus a ${BUILD_RESOURCE_DISK_RESERVE_KB} KiB safety reserve; ${disk_available_kb} KiB is available."
        return 1
    fi

    logW "Temporary build swap may be slow and can increase flash-media writes."
    if [[ "$DRY_RUN" == "true" ]]; then
        logI "Dry run: would create ${allocation_kb} KiB of protected temporary build swap beneath ${BUILD_RESOURCE_SWAP_ROOT}."
        logI "Dry run: would disable and remove only that invocation-owned swap after the installer exits."
        return 0
    fi

    for tool in df dirname stat install mktemp chmod fallocate mkswap swapon swapoff rm rmdir; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            logE "Cannot create temporary build swap because '$tool' is unavailable."
            return 1
        fi
    done

    if [[ -e "$BUILD_RESOURCE_SWAP_ROOT" || -L "$BUILD_RESOURCE_SWAP_ROOT" ]]; then
        if [[ ! -d "$BUILD_RESOURCE_SWAP_ROOT" || -L "$BUILD_RESOURCE_SWAP_ROOT" ]]; then
            logE "Temporary build swap root is not a directory: $BUILD_RESOURCE_SWAP_ROOT"
            return 1
        fi
        directory_mode=$(stat -c '%a' "$BUILD_RESOURCE_SWAP_ROOT" 2>/dev/null || true)
        directory_owner=$(stat -c '%u' "$BUILD_RESOURCE_SWAP_ROOT" 2>/dev/null || true)
        if [[ ! "$directory_mode" =~ ^[0-7]+$ ||
            ! "$directory_owner" =~ ^[0-9]+$ ||
            "$directory_owner" -ne "$EUID" ||
            $((8#$directory_mode & 077)) -ne 0 ]]; then
            logE "Temporary build swap root must be owned by the installer user and inaccessible to group/other: $BUILD_RESOURCE_SWAP_ROOT"
            return 1
        fi
    else
        exec_command "Create temporary build swap directory" \
            install -d -m 0700 "$BUILD_RESOURCE_SWAP_ROOT" || return 1
        TEMP_BUILD_SWAP_ROOT_OWNED="true"
    fi

    TEMP_BUILD_SWAP_PATH=$(mktemp "${BUILD_RESOURCE_SWAP_ROOT}/wsprrypi-build.XXXXXXXX.swap") || {
        cleanup_temp_build_swap || true
        return 1
    }
    TEMP_BUILD_SWAP_OWNED="true"
    exec_command "Protect temporary build swap file" chmod 0600 "$TEMP_BUILD_SWAP_PATH" || {
        cleanup_temp_build_swap || true
        return 1
    }
    exec_command "Allocate temporary build swap" fallocate -l "${allocation_kb}K" -- "$TEMP_BUILD_SWAP_PATH" || {
        cleanup_temp_build_swap || true
        return 1
    }
    exec_command "Format temporary build swap" mkswap "$TEMP_BUILD_SWAP_PATH" || {
        cleanup_temp_build_swap || true
        return 1
    }
    exec_command "Enable temporary build swap" swapon -p 10 "$TEMP_BUILD_SWAP_PATH" || {
        cleanup_temp_build_swap || true
        return 1
    }
    TEMP_BUILD_SWAP_ACTIVE="true"

    if ! temp_build_swap_is_active "$TEMP_BUILD_SWAP_PATH"; then
        logE "Temporary build swap did not appear in the active swap inventory."
        cleanup_temp_build_swap || true
        return 1
    fi

    logI "Enabled ${allocation_kb} KiB of invocation-owned temporary build swap."
    return 0
}

# -----------------------------------------------------------------------------
# @brief Enforce deterministic build resources before installer mutation.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
preflight_build_resources() {
    if [[ "$BINARY_SOURCE" != "build" ]] && ! rp1_gpclk_dkms_installation_selected; then
        logI "Precompiled executable: skipping application build and build swap."
        return 0
    fi
    local mem_total_kb
    local mem_available_kb
    local swap_free_kb
    local zram_swap_free_kb
    local file_swap_free_kb
    local partition_swap_free_kb
    local other_swap_free_kb
    local non_zram_swap_free_kb
    local required_swap_kb

    case "$ALLOW_TEMP_BUILD_SWAP" in
        auto|true|false) ;;
        *)
            logE "ALLOW_TEMP_BUILD_SWAP must be 'auto', 'true', or 'false'."
            return 1
            ;;
    esac

    mem_total_kb=$(build_resource_meminfo_kb "$BUILD_RESOURCE_MEMINFO_PATH" MemTotal) || {
        logE "Unable to read MemTotal from $BUILD_RESOURCE_MEMINFO_PATH."
        return 1
    }
    mem_available_kb=$(build_resource_meminfo_kb "$BUILD_RESOURCE_MEMINFO_PATH" MemAvailable) || {
        logE "Unable to read MemAvailable from $BUILD_RESOURCE_MEMINFO_PATH."
        return 1
    }
    if [[ ! "$mem_total_kb" =~ ^[0-9]+$ || "$mem_total_kb" -le 0 ||
        ! "$mem_available_kb" =~ ^[0-9]+$ ]]; then
        logE "Invalid memory values reported by $BUILD_RESOURCE_MEMINFO_PATH."
        return 1
    fi

    IFS=' ' read -r swap_free_kb zram_swap_free_kb file_swap_free_kb \
        partition_swap_free_kb other_swap_free_kb non_zram_swap_free_kb < <(
        build_resource_swap_free_kb "$BUILD_RESOURCE_SWAPS_PATH"
    ) || {
        logE "Unable to inventory active swap from $BUILD_RESOURCE_SWAPS_PATH."
        return 1
    }
    required_swap_kb=$(required_non_zram_build_swap_kb "$mem_total_kb")

    logI "Build resources:"
    logI "  MemTotal=${mem_total_kb} KiB"
    logI "  MemAvailable=${mem_available_kb} KiB"
    logI "  swap-free=${swap_free_kb} KiB"
    logI "Build swap classification:"
    logI "  zram-free=${zram_swap_free_kb} KiB"
    logI "  file-free=${file_swap_free_kb} KiB"
    logI "  partition-free=${partition_swap_free_kb} KiB"
    logI "  other-independent-free=${other_swap_free_kb} KiB"
    logI "Build swap independently-backed-free=${non_zram_swap_free_kb} KiB."

    if ((mem_total_kb < BUILD_RESOURCE_LOW_RAM_KB)); then
        if [[ "${JOBS:-}" != "1" ]]; then
            logW "Low-memory build detected; forcing single-job compilation."
        fi
        export JOBS=1
    fi

    if ((required_swap_kb == 0)); then
        return 0
    fi
    if ((non_zram_swap_free_kb + BUILD_RESOURCE_SWAP_METADATA_ALLOWANCE_KB >=
        required_swap_kb)); then
        logI "Low-memory build has sufficient independently backed swap."
        return 0
    fi

    if ((zram_swap_free_kb > 0)); then
        logW "Active zram swap is not counted as independently backed build swap."
    fi
    if [[ "$ALLOW_TEMP_BUILD_SWAP" == "false" ]]; then
        logE "Low-memory build requires at least ${required_swap_kb} KiB of free non-zram swap; ${non_zram_swap_free_kb} KiB is available."
        logE "Enable adequate file/partition swap, or explicitly retry with ALLOW_TEMP_BUILD_SWAP=true."
        return 1
    fi

    if [[ "$ALLOW_TEMP_BUILD_SWAP" == "auto" ]]; then
        logI "Automatic temporary build swap selected for a low-memory build with insufficient independently backed swap."
    fi
    create_temp_build_swap "$required_swap_kb" "$non_zram_swap_free_kb"
}

# -----------------------------------------------------------------------------
# @brief  Analyze system resources and display compilation concurrency notes.
# @details
#   Evaluates the system environment using the same heuristics as the Makefile
#   JOBS logic, but is strictly informational.
#
#   Signals considered:
#     - Total RAM (MemTotal)
#     - Available RAM at runtime (MemAvailable)
#     - Swap availability
#     - CPU core count
#     - Presence of a running desktop environment
#
#   This function does NOT return or export a JOBS value. It is intended solely
#   to explain expected compilation behavior and potential performance impacts
#   to the user.
#
#   Overrides respected (for explanation only):
#     - JOBS            Environment override
#     - MIN_MEM_AVAIL_KB
#     - GUI_CLAMP
#
# @stdout Human-readable informational and warning logs only.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
display_compilation_resource_notes() {
    [[ "$BINARY_SOURCE" == "build" ]] || return 0
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local mem_kb
    local mem_avail_kb
    local swap_kb
    local nprocs
    local one_point_five_gib_kb
    local three_point_five_gib_kb
    local min_mem_avail_kb
    local gui_clamp
    local jobs_estimate

    mem_kb=$(awk '/^MemTotal:/ {print $2}' /proc/meminfo 2>/dev/null)
    mem_avail_kb=$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo 2>/dev/null)
    swap_kb=$(awk '/^SwapTotal:/ {print $2}' /proc/meminfo 2>/dev/null)
    nprocs=$(nproc 2>/dev/null)

    [[ -z "$mem_kb" ]] && mem_kb=0
    [[ -z "$mem_avail_kb" ]] && mem_avail_kb=0
    [[ -z "$swap_kb" ]] && swap_kb=0
    [[ -z "$nprocs" ]] && nprocs=1

    one_point_five_gib_kb=$((1536 * 1024))
    three_point_five_gib_kb=$((3584 * 1024))
    min_mem_avail_kb="${MIN_MEM_AVAIL_KB:-$((1280 * 1024))}"
    gui_clamp="${GUI_CLAMP:-1}"

    # Determine an estimated job count (for explanation only)
    if [[ -n "${JOBS:-}" ]]; then
        jobs_estimate="$JOBS"
        (( jobs_estimate < 1 )) && jobs_estimate=1
        logI "JOBS override detected: ${jobs_estimate}."
    elif (( mem_avail_kb > 0 && mem_avail_kb < min_mem_avail_kb )); then
        jobs_estimate=1
        logW "Low available memory detected at build time."
    elif (( mem_kb < one_point_five_gib_kb )); then
        jobs_estimate=1
        logW "System has less than 1.5 GiB total RAM."
    elif (( swap_kb == 0 && mem_kb < three_point_five_gib_kb )); then
        jobs_estimate=1
        logW "Swap is disabled on a low-memory system."
    elif (( mem_kb < three_point_five_gib_kb )); then
        jobs_estimate=$((nprocs / 2))
        (( jobs_estimate < 1 )) && jobs_estimate=1
        logI "Moderate RAM system detected; limiting parallelism."
    else
        jobs_estimate="$nprocs"
        logI "Sufficient RAM detected; full parallelism likely."
    fi

    # Desktop / GUI hint
    if [[ "$gui_clamp" -eq 1 ]]; then
        if pgrep -x Xorg        >/dev/null 2>&1 || \
           pgrep -x Xwayland    >/dev/null 2>&1 || \
           pgrep -x wayfire     >/dev/null 2>&1 || \
           pgrep -x weston      >/dev/null 2>&1 || \
           pgrep -x gnome-shell >/dev/null 2>&1 || \
           pgrep -x lightdm     >/dev/null 2>&1 || \
           pgrep -x gdm3        >/dev/null 2>&1 || \
           pgrep -x sddm        >/dev/null 2>&1 || \
           pgrep -x lxdm        >/dev/null 2>&1; then
            logW "A desktop environment is running; compilation may be slower."
            jobs_estimate=$((jobs_estimate / 2))
            (( jobs_estimate < 1 )) && jobs_estimate=1
        fi
    fi

    # User-facing timing guidance
    if (( mem_kb < one_point_five_gib_kb )); then
        logW "Compilation may take from 10m (Pi 3) up to 50m (Pi 1)."
    elif (( mem_kb < (3072 * 1024) )); then
        logW "Compilation may take more than 10m."
    else
        logI "Compilation should typically complete in around 5m."
    fi

    logI "Estimated effective parallelism: ${jobs_estimate} job(s)."

    # Intentionally no stdout return; informational only.
    debug_end "$debug"
}

# -----------------------------------------------------------------------------
# @brief Compile and stage a binary executable.
#
# @details
#   `compile_binary()` takes an optional debug flag and an executable name,
#   determines whether to build the debug or release target based on the
#   executable’s “_debug” suffix, runs `make` in the source directory, and
#   then copies the resulting binary into the staging directory. In DRY_RUN
#   mode, it prints the commands instead of executing them.
#
# @global LOCAL_SOURCE_DIR       Directory containing the source for `make`.
# @global DRY_RUN                If "true", commands are only logged (no exec).
#
# @param $1  Debug flag for enabling or disabling debug output.
# @param $2  Executable name (with optional path/extension) to compile.
#
# @throws Exits with status 1 if arguments are missing, compilation fails,
#         or moving the binary fails.
# @return Returns 0 on success, 1 on error.
#
# @example
#   compile_binary "wsprrypi" "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
# Precompiled executable acquisition is explicit and never falls back to a build.
set_binary_source() { BINARY_SOURCE="$1"; }
set_binary_path() { BINARY_PATH="$1"; }
set_binary_release_tag() { BINARY_RELEASE_TAG="$1"; }

# Keep helper diagnostics in the installer logger and successful field values
# separate from console output. An empty first argument discards helper output.
run_precompiled_helper() {
    local precompiled_output_var="$1" precompiled_output precompiled_status precompiled_line
    shift
    if [[ -n "$precompiled_output_var" ]]; then
        printf -v "$precompiled_output_var" '%s' ''
    fi
    if precompiled_output=$(python3 "$@" 2>&1); then
        if [[ -n "$precompiled_output_var" ]]; then
            printf -v "$precompiled_output_var" '%s' "$precompiled_output"
        fi
        return 0
    else
        precompiled_status=$?
    fi
    if [[ -n "$precompiled_output" ]]; then
        while IFS= read -r precompiled_line; do
            [[ -z "$precompiled_line" ]] || logE "$precompiled_line"
        done <<<"$precompiled_output"
    else
        logE "Precompiled executable helper failed (status $precompiled_status)."
    fi
    return "$precompiled_status"
}

validate_binary_options() {
    case "$BINARY_SOURCE" in
        build)
            [[ -z "$BINARY_PATH$BINARY_RELEASE_TAG" ]] || {
                logE "Binary path/tag options require --binary-source local or release."; return 1;
            } ;;
        local)
            [[ -z "$BINARY_RELEASE_TAG" ]] || { logE "--release-tag requires --binary-source release."; return 1; } ;;
        release)
            [[ -n "$BINARY_RELEASE_TAG" && -z "$BINARY_PATH" ]] || {
                logE "Release mode requires --release-tag and cannot use a local binary path."; return 1;
            }
            [[ "$BINARY_RELEASE_TAG" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]] || { logE "Invalid release tag."; return 1; }
            ;;
        *) logE "--binary-source must be build, local, or release."; return 1 ;;
    esac
    if [[ "$ACTION" == "uninstall" && "$BINARY_SOURCE" != "build" ]]; then
        logE "Uninstall does not take executable-source options. Run uninstall without them."
        return 1
    fi
}

cleanup_precompiled_executable() {
    if [[ -n "$BINARY_PREVIOUS" && "$BINARY_INSTALLED" != "recovery-failed" ]]; then
        rm -f -- "$BINARY_PREVIOUS"
        BINARY_PREVIOUS=""
    fi
    if [[ -n "$BINARY_STAGE" ]]; then
        rm -rf -- "$BINARY_STAGE"
        BINARY_STAGE=""
    fi
}

rollback_executable() {
    [[ "$BINARY_INSTALLED" == "true" ]] || return 0
    if systemctl is-active --quiet "$WSPR_SERVICE" 2>/dev/null; then
        exec_command "Stop $WSPR_SERVICE for executable recovery" \
            systemctl stop "$WSPR_SERVICE" || { BINARY_INSTALLED="recovery-failed"; return 1; }
    fi
    if [[ -n "$BINARY_PREVIOUS" ]]; then
        if ! exec_command "Restore previous executable at $BINARY_DESTINATION" \
            mv -f -- "$BINARY_PREVIOUS" "$BINARY_DESTINATION"; then
            logE "Executable recovery failed; previous executable retained at $BINARY_PREVIOUS."
            BINARY_INSTALLED="recovery-failed"
            return 1
        fi
        BINARY_PREVIOUS=""
    else
        exec_command "Remove failed executable at $BINARY_DESTINATION" \
            rm -f -- "$BINARY_DESTINATION" || return 1
    fi
    BINARY_INSTALLED="false"
    logI "Restored the previous executable after installation failure."
}

prepare_precompiled_executable() {
    [[ "$BINARY_SOURCE" != "build" ]] || return 0
    local helper binary packages
    helper="${LOCAL_REPO_DIR}/scripts/precompiled_binary.py"
    binary="${BINARY_PATH:-${USER_HOME}/wsprrypi}"
    if [[ "$DRY_RUN" == "true" ]]; then
        logI "Dry run: would validate and stage the $BINARY_SOURCE executable; no download or copy performed."
        if [[ "$BINARY_SOURCE" == "local" && -f "$helper" ]]; then
            run_precompiled_helper '' "$helper" check --binary "$binary" || return 1
            BINARY_VERSION="dry-run-unvalidated"
            run_precompiled_helper packages "$helper" check --binary "$binary" --field runtime_packages || return 1
            mapfile -t BINARY_RUNTIME_PACKAGES <<<"$packages"
        else
            BINARY_VERSION="dry-run-unvalidated"
            logI "Dry run: artifact version and dependencies require validation during a real installation."
        fi
        return 0
    fi
    git_clone "$@" || return 1
    [[ -f "$helper" ]] || { logE "Missing precompiled validation helper in the source checkout."; return 1; }
    BINARY_STAGE=$(mktemp -d /var/tmp/wsprrypi-binary.XXXXXXXX) || return 1
    exec_command "Set precompiled staging directory permissions" chmod 755 "$BINARY_STAGE" "$@" || return 1
    if [[ "$BINARY_SOURCE" == "local" ]]; then
        [[ -f "$binary" ]] || {
            logE "Provide the executable at $binary."; return 1;
        }
        # Caller-owned files are copied, never removed, and checked after copying.
        exec_command "Stage precompiled executable" cp -- "$binary" "$BINARY_STAGE/wsprrypi" "$@" || return 1
    else
        run_precompiled_helper '' "$helper" fetch --repo "$REPO_ORG/$REPO_NAME" --tag "$BINARY_RELEASE_TAG" --directory "$BINARY_STAGE" || return 1
    fi
    exec_command "Set precompiled executable permissions" chmod 755 "$BINARY_STAGE/wsprrypi" "$@" || return 1
    run_precompiled_helper packages "$helper" check --binary "$BINARY_STAGE/wsprrypi" --field runtime_packages || return 1
    mapfile -t BINARY_RUNTIME_PACKAGES <<<"$packages"
    logI "Checked $BINARY_SOURCE executable architecture; application build disabled."
}

validate_precompiled_runtime() {
    [[ "$BINARY_SOURCE" != "build" ]] || return 0
    [[ "$DRY_RUN" != "true" ]] || { logI "Dry run: runtime library checks await installed dependencies."; return 0; }
    run_precompiled_helper BINARY_VERSION "$LOCAL_REPO_DIR/scripts/precompiled_binary.py" check \
        --binary "$BINARY_STAGE/wsprrypi" --full --runtime-user "$SUDO_USER" --field version || return 1
}

prepare_install_executable() {
    if [[ "$BINARY_SOURCE" == "build" ]]; then
        compile_binary "$@"
    else
        validate_precompiled_runtime "$@"
    fi
}

stop_binary_service() {
    if systemctl is-active --quiet "$WSPR_SERVICE" 2>/dev/null; then
        WAS_RUNNING="true"
        exec_command "Stop ${WSPR_SERVICE}.service" systemctl stop "${WSPR_SERVICE}.service" "$@" || return 1
    fi
}

compile_binary() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Ensure the executable argument is provided
    if [[ -z "$1" ]]; then
        logE "Error: Missing required arguments."
        logE "Usage: compile_binary <executable>"
        debug_end "$debug"
        return 1
    fi

    # Declare local variables
    local executable exe_name compiled_path staging_path type daemon_name

    # Get from args or associative array
    executable="$1"
    exe_name="${executable##*/}" # Remove path
    exe_name="${exe_name%.*}"    # Remove extension (if present)
    compiled_path="${LOCAL_SOURCE_DIR}/build/bin/${executable}"
    staging_path="${LOCAL_REPO_DIR}/executables/"
    daemon_name="${WSPR_SERVICE}" # Remove path
    daemon_systemd_name="${daemon_name}.service"

    exec_command "Create executable staging directory" \
        runuser -u "$SUDO_USER" -- mkdir -p "${staging_path}" "$debug" || {
        logE "Error: Unable to create executable staging directory."
        debug_end "$debug"
        return 1
    }
    if [[ "$DRY_RUN" != "true" && ! -d "${staging_path:-}" ]]; then
        debug_end "$debug"
        die 1 "Executables source directory does not exist."
    fi

    type="${WSPR_BUILD_TYPE}"
    if [[ "${type}" != "DEBUG" && "${type}" != "RELEASE" ]]; then
        logE "Error: Invalid WSPR_BUILD_TYPE '${type}'."
        debug_end "$debug"
        return 1
    fi

    # Compile the binary
    debug_print "Compiling binary." "$debug"
    if [[ "$DRY_RUN" == "true" ]]; then
        logD "Exec: (cd ${LOCAL_SOURCE_DIR} && make ${type})"
    else
        exec_command "Compile ${type,,} binary" runuser -u "$SUDO_USER" -- make -C "${LOCAL_SOURCE_DIR}" "${type}" "$debug" || {
            logE "Error: Unable to compile binary."
            debug_end "$debug"
            return 1
        }
    fi

    # Move the binary
    debug_print "Moving binary to staging." "$debug"
    if [[ "$DRY_RUN" == "true" ]]; then
        logD "Exec: cp -f $compiled_path $staging_path"
    else
        exec_command "Move binary to staging" cp -f "${compiled_path}" "${staging_path}" "$debug" || {
            logE "Error: Unable to move compiled binary."
            debug_end "$debug"
            return 1
        }
    fi

    debug_end "$debug"
}

# -----------------------------------------------------------------------------
# @brief Manages the installation or removal of an executable.
# @details This function installs or removes an executable binary in
#          `/usr/local/bin/`. During installation, it verifies the existence
#          of the source file, copies it, sets ownership, and adjusts
#          permissions. During uninstallation, it removes the executable.
#
# @global ACTION Specifies whether the function runs in 'install' or 'uninstall' mode.
# @global USER_HOME The user's home directory path.
# @global REPO_NAME The name of the repository.
# @global DRY_RUN If set to "true", performs a dry-run without making changes.
#
# @param $1 The name of the executable file.
# @param $2 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if required arguments are missing, the source file
#         is not found, or an operation fails.
#
# @return Returns 0 on success, or 1 if any operation fails.
#
# @example
#   manage_exe "wsprrypi" "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
manage_exe() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Ensure the executable argument is provided
    if [[ -z "$1" ]]; then
        logE "Error: Missing required arguments."
        logE "Usage: manage_exe <exe_name>"
        debug_end "$debug"
        return 1
    fi

    # Declare local variables
    local executable exe_name source_path exe_path

    # Get from args or associative array
    executable="$1"
    exe_name="${executable##*/}" # Remove path
    exe_name="${exe_name%.*}"    # Remove extension (if present)
    source_path="${LOCAL_REPO_DIR}/executables/${exe_name}"
    exe_path="${BINARY_INSTALL_DIR}/${executable}"

    if [[ "$ACTION" == "install" ]]; then
        if [[ "$BINARY_SOURCE" != "build" ]]; then
            source_path="${BINARY_STAGE}/wsprrypi"
        fi
        if [[ "$DRY_RUN" == "true" ]]; then
            logI "Dry run: would install the validated executable at $exe_path."
            return 0
        fi
        [[ -f "$source_path" ]] || { logE "Executable is missing: $source_path"; return 1; }
        local pending
        pending=$(mktemp "${exe_path}.new.XXXXXXXX") || return 1
        if ! exec_command "Stage executable for $exe_path" \
            install -o root -g root -m 755 "$source_path" "$pending" "$debug"; then
            rm -f -- "$pending"
            return 1
        fi
        BINARY_DESTINATION="$exe_path"
        if [[ -e "$exe_path" || -L "$exe_path" ]]; then
            BINARY_PREVIOUS=$(mktemp "${exe_path}.previous.XXXXXXXX") || { rm -f -- "$pending"; return 1; }
            exec_command "Back up existing executable at $exe_path" \
                cp -p -- "$exe_path" "$BINARY_PREVIOUS" "$debug" || { rm -f -- "$pending" "$BINARY_PREVIOUS"; BINARY_PREVIOUS=""; return 1; }
        fi
        if ! stop_binary_service "$debug"; then
            rm -f -- "$pending"
            return 1
        fi
        if ! exec_command "Install executable at $exe_path" \
            mv -f -- "$pending" "$exe_path" "$debug"; then
            rm -f -- "$pending"
            restore_daemon_state "$debug"
            return 1
        fi
        BINARY_INSTALLED="true"

    elif [[ "$ACTION" == "uninstall" ]]; then
        # Remove the application
        debug_print "Removing application." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: rm -f $exe_path"
        else
            exec_command "Remove application" rm -f "${exe_path}" "$debug" || {
                logE "Failed to remove application."
                debug_end "$debug"
                return 1
            }
        fi
    else
        die 1 "Invalid action. Use 'install' or 'uninstall'."
    fi

    debug_end "$debug"
}

# -----------------------------------------------------------------------------
# @brief Installs or removes the private support-bundle collector runtime.
# @details Storage is intentionally retained on uninstall. Administrators may
#          remove `/var/lib/wsprrypi/support-bundles` explicitly after retaining
#          or deleting any diagnostic artifacts they need.
# -----------------------------------------------------------------------------
manage_support_bundle_runtime() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # shellcheck source=scripts/support_bundle_runtime_install.sh
    source "${LOCAL_REPO_DIR}/scripts/support_bundle_runtime_install.sh" || {
        logE "Failed to load support-bundle runtime provisioning."
        debug_end "$debug"
        return 1
    }
    if ! support_bundle_runtime_provision "$ACTION" "$LOCAL_REPO_DIR" "" "$debug"; then
        logE "Failed to provision support-bundle runtime."
        debug_end "$debug"
        return 1
    fi

    debug_end "$debug"
}

# Install the application-owned route companion from the selected checkout.
manage_route_application() {
    if [[ "$ACTION" == "install" ]]; then
        exec_command "Create route-runtime directory" \
            install -d -o root -g root -m 0755 /usr/local/lib/wsprrypi "$@" || return 1
        exec_command "Install route application companion" \
            install -o root -g root -m 0755 "${LOCAL_REPO_DIR}/scripts/route_application.py" \
            /usr/local/lib/wsprrypi/route_application.py "$@" || return 1
        exec_command "Install runtime reconciliation support" \
            python3 "${LOCAL_REPO_DIR}/scripts/install_runtime_reconcile.py" install "$@" || return 1
    elif [[ "$ACTION" == "uninstall" ]]; then
        exec_command "Remove runtime reconciliation support" \
            python3 "${LOCAL_REPO_DIR}/scripts/install_runtime_reconcile.py" remove "$@" || return 1
    fi
}

# -----------------------------------------------------------------------------
# @brief Manages the installation or removal of configuration files.
# @details This function installs or removes a configuration file. During
#          installation, it validates the source file, updates the semantic
#          version placeholder, and sets appropriate permissions. During
#          uninstallation, it removes the configuration file. For the main
#          WSPR INI file, it also installs a stock copy with ".stock"
#          appended to the destination filename.
#
# @global ACTION Specifies whether the function runs in 'install' or
#         'uninstall' mode.
# @global USER_HOME The user's home directory path.
# @global REPO_NAME The name of the repository.
# @global DRY_RUN If set to "true", performs a dry-run without making
#         changes.
# @global LOCAL_CONFIG_DIR The local configuration directory path.
# @global WSPR_INI The primary WSPR INI filename.
#
# @param $1 The name of the configuration file.
# @param $2 The destination directory for the configuration file.
# @param $3 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if required arguments are missing, the
#         source file is not found, or the target directory does not
#         exist.
#
# @return Returns 0 on success, or 1 if any operation fails.
#
# @example
# manage_config "wsprrypi.ini" "/usr/local/etc" "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
manage_config() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local config_file
    local config_name
    local config_path
    local source_path
    local stock_source_path=""
    local stock_config_path=""
    local retval=0

    # Ensure the configuration arguments are provided
    if [[ -z "$1" || -z "$2" ]]; then
        logE "Error: Missing required arguments."
        logE "Usage: manage_config <config_file> <config_path>"
        debug_end "$debug"
        return 1
    fi

    # Get from args or associative array
    config_file="$1"
    config_path="$2"
    config_name="${config_file##*/}"
    config_name="${config_name%.*}"
    source_path="${LOCAL_CONFIG_DIR}/${config_file}"

    config_path="${config_path}/${config_file}"

    if [[ "$ACTION" == "install" ]]; then
        # Validate source file exists
        if [[ ! -f "$source_path" ]]; then
            logE "Error: Source file '$source_path' not found."
            debug_end "$debug"
            return 1
        fi

        # Validate target directory exists
        if [[ ! -d "$(dirname "$config_path")" ]]; then
            logE "Error: Target directory '$(dirname "$config_path")' does not exist."
            debug_end "$debug"
            return 1
        fi

        # Save the stock source and stock destination for the primary INI
        if [[ "${config_file}" == "$WSPR_INI" ]]; then
            stock_source_path="$source_path"
            stock_config_path="${config_path}.stock"
        fi

        # If we are doing the INI file, see if we can merge
        if [[ "${config_file}" == "$WSPR_INI" ]]; then
            local old_path=""
            local merged_ini=""

            if [[ -f "/usr/local/etc/wspr.ini" ]]; then
                old_path="/usr/local/etc/wspr.ini"
            elif [[ -f "/usr/local/etc/wsprrypi.ini" ]]; then
                old_path="/usr/local/etc/wsprrypi.ini"
            fi

            if [[ -n "$old_path" ]]; then
                merged_ini="${LOCAL_CONFIG_DIR}/wsprrypi_merged.ini"
                if upgrade_ini "$old_path" "$source_path" "$merged_ini" "$debug"; then
                    source_path="$merged_ini"
                else
                    debug_end "$debug"
                    return 1
                fi
            fi
        fi

        # Install the active configuration
        debug_print \
            "Copying configuration from $source_path to $config_path." \
            "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: cp -f $source_path $config_path"
        else
            exec_command \
                "Install configuration" \
                cp -f "${source_path}" "${config_path}" "$debug" \
                || retval=1
        fi

        # Update version in the active configuration
        replace_string_in_script \
            "$config_path" \
            "SEMANTIC_VERSION" \
            "$SEM_VER" \
            "$debug" || retval=1

        # Install the stock configuration copy for the primary INI
        if [[ -n "$stock_source_path" && -n "$stock_config_path" ]]; then
            debug_print \
                "Copying stock configuration from $stock_source_path to $stock_config_path." \
                "$debug"
            if [[ "$DRY_RUN" == "true" ]]; then
                logD "Exec: cp -f $stock_source_path $stock_config_path"
            else
                exec_command \
                    "Install stock configuration" \
                    cp -f "${stock_source_path}" "${stock_config_path}" "$debug" \
                    || retval=1
            fi

            replace_string_in_script \
                "$stock_config_path" \
                "SEMANTIC_VERSION" \
                "$SEM_VER" \
                "$debug" || retval=1

            debug_print \
                "Changing ownership on stock configuration." \
                "$debug"
            if [[ "$DRY_RUN" == "true" ]]; then
                logD "Exec: chown root:root $stock_config_path"
            else
                exec_command \
                    "Change ownership on stock configuration" \
                    chown root:root "${stock_config_path}" "$debug" \
                    || retval=1
            fi

            debug_print \
                "Changing permissions on stock configuration." \
                "$debug"
            if [[ "$DRY_RUN" == "true" ]]; then
                logD "Exec: chmod 644 $stock_config_path"
            else
                exec_command \
                    "Set stock config permissions" \
                    chmod 644 "${stock_config_path}" "$debug" \
                    || retval=1
            fi
        fi

        # Change ownership on the active configuration
        debug_print "Changing ownership on configuration." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: chown root:root $config_path"
        else
            exec_command \
                "Change ownership on configuration" \
                chown root:root "${config_path}" "$debug" \
                || retval=1
        fi

        # Change permissions on the active configuration
        debug_print "Changing permissions on configuration." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: chmod 644 $config_path"
        else
            exec_command \
                "Set config permissions" \
                chmod 644 "${config_path}" "$debug" \
                || retval=1
        fi

    elif [[ "$ACTION" == "uninstall" ]]; then
        # Remove the active configuration
        debug_print "Removing configuration." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: rm -f $config_path"
        else
            exec_command \
                "Remove configuration" \
                rm -f "${config_path}" "$debug" \
                || retval=1
        fi

        # Remove the stock configuration copy for the primary INI
        if [[ "${config_file}" == "$WSPR_INI" ]]; then
            stock_config_path="${config_path}.stock"
            debug_print "Removing stock configuration." "$debug"
            if [[ "$DRY_RUN" == "true" ]]; then
                logD "Exec: rm -f $stock_config_path"
            else
                exec_command \
                    "Remove stock configuration" \
                    rm -f "${stock_config_path}" "$debug" \
                    || retval=1
            fi
        fi
    else
        die 1 "Invalid action. Use 'install' or 'uninstall'."
    fi

    debug_end "$debug"
    return "$retval"
}

# -----------------------------------------------------------------------------
# @brief Migrate values from an old INI into a new INI file.
# @details Reads values from an old INI file, ignores comments there, and merges
#   those values into a new INI file while preserving the new file's formatting
#   and inline comments. Matching is section-aware, so repeated key names in
#   different sections do not overwrite each other. A backup of the previous
#   INI is retained alongside the original file before the merged file is used.
#
# @param $1 Path to the old INI file containing original values.
# @param $2 Path to the new INI file to merge into.
# @param $3 Path for the output merged INI file.
#
# @return Returns 0 on success, exits non-zero on failure.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
upgrade_ini() {
    local debug old_ini new_ini merged_ini rc backup_ini tmp_merged err_details
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    old_ini="$1"
    new_ini="$2"
    merged_ini="$3"
    backup_ini="${old_ini}.pre_migration.bak"
    tmp_merged="${merged_ini}.tmp"

    debug_print "Merging $old_ini -> $merged_ini." "$debug"

    if [[ ! -f "$new_ini" ]]; then
        logE "New INI file not found: $new_ini"
        debug_end "$debug"
        return 1
    fi

    if [[ ! -s "$new_ini" ]]; then
        logE "New INI file is empty: $new_ini"
        debug_end "$debug"
        return 1
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        local planned_command
        if [[ -f "$old_ini" ]]; then
            planned_command=$(render_command_argv cp -f "$old_ini" "$backup_ini")
            debug_print "Dry run command: $planned_command" "$debug"
        fi
        debug_print \
            "Dry run INI merge: old=$(printf '%q' "$old_ini") new=$(printf '%q' "$new_ini") output=$(printf '%q' "$merged_ini")" \
            "$debug"
        debug_end "$debug"
        return 0
    fi

    if [[ -f "$old_ini" ]]; then
        exec_command \
            "Backup existing INI" \
            cp -f "$old_ini" "$backup_ini" "$debug" || {
            logE "INI backup failed."
            debug_end "$debug"
            return 1
        }
    fi

    rm -f /tmp/upgrade_ini.err "$tmp_merged"

    mawk '
    function trim(value) {
      gsub(/^[ \t]+|[ \t]+$/, "", value)
      return value
    }

    # Phase 1: read old.ini → overrides[section,key]=value
    NR == FNR {
      if ($0 ~ /^[[:space:]]*([;#]|$)/) next

      if ($0 ~ /^[[:space:]]*\[/) {
        section = $0
        sub(/^[[:space:]]*\[/, "", section)
        sub(/\][[:space:]]*$/, "", section)
        section = trim(section)
        current_section = section
        next
      }

      l = $0
      sub(/[;#].*$/, "", l)
      if (l !~ /=/) next

      key = l
      sub(/=.*/, "", key)
      key = trim(key)

      val = l
      sub(/^[^=]*=[ \t]*/, "", val)
      sub(/[ \t]*$/, "", val)

      overrides[current_section SUBSEP key] = val
      next
    }

    # Phase 2: walk new.ini
    {
      if ($0 ~ /^[[:space:]]*[;#]/ || $0 ~ /^[[:space:]]*$/) {
        print
        next
      }

      if ($0 ~ /^[[:space:]]*\[/) {
        section = $0
        sub(/^[[:space:]]*\[/, "", section)
        sub(/\][[:space:]]*$/, "", section)
        section = trim(section)
        current_section = section
        print
        next
      }

      line = $0
      comment = ""
      p = match(line, /;|#/)
      if (p) {
        comment = substr(line, p)
        line = substr(line, 1, p - 1)
      }

      eq = match(line, /=/)
      if (eq) {
        left = substr(line, 1, eq - 1)
        orig = substr(line, eq + 1)
        keytrim = trim(left)
        composite = current_section SUBSEP keytrim

        if (composite in overrides) {
          tmp = orig
          gsub(/^[ \t]*/, "", tmp)
          leadWS = substr(orig, 1, length(orig) - length(tmp))

          tmp2 = tmp
          gsub(/[ \t]*$/, "", tmp2)
          trailerWS = substr(tmp, length(tmp2) + 1)

          printf("%s=%s%s%s%s\n",
                 left, leadWS, overrides[composite], trailerWS, comment)
          next
        }
      }

      print
    }
    ' "$old_ini" "$new_ini" >"$tmp_merged" 2>/tmp/upgrade_ini.err

    rc=$?

    if (( rc != 0 )); then
        err_details=$(sed 's/^/  > /' /tmp/upgrade_ini.err)
        logE "INI merge failed (mawk exited $rc). Falling back to default INI." "$err_details"

        if [[ -s "$new_ini" ]]; then
            exec_command "Use default INI after merge failure" \
                cp -f "$new_ini" "$merged_ini" "$debug" || return 1
            rm -f /tmp/upgrade_ini.err "$tmp_merged"
            debug_print "Fallback to installer INI succeeded." "$debug"
            debug_end "$debug"
            return 0
        else
            logE "Fallback failed: installer INI is also empty."
            rm -f /tmp/upgrade_ini.err "$tmp_merged"
            debug_end "$debug"
            return 1
        fi
    fi

    if [[ ! -s "$tmp_merged" ]]; then
        logE "INI merge produced an empty file. Falling back to default INI."

        if [[ -s "$new_ini" ]]; then
            exec_command "Use default INI after empty merge" \
                cp -f "$new_ini" "$merged_ini" "$debug" || return 1
            debug_print "Fallback to installer INI succeeded." "$debug"
        else
            logE "Fallback failed: installer INI is also empty."
            rm -f /tmp/upgrade_ini.err "$tmp_merged"
            debug_end "$debug"
            return 1
        fi
    else
        exec_command "Publish merged INI" mv -f "$tmp_merged" "$merged_ini" "$debug" || return 1
        debug_print "INI merge successful." "$debug"
    fi

    rm -f /tmp/upgrade_ini.err "$tmp_merged"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Manages the installation or removal of a systemd service.
# @details This function installs or removes a systemd service, including
#          handling service enablement, starting, stopping, and proper
#          logging setup. It also ensures the appropriate configuration
#          for system logs based on the use of syslog.
#
# @global ACTION Specifies whether the function runs in 'install' or 'uninstall' mode.
# @global USER_HOME The user's home directory path.
# @global REPO_NAME The name of the repository.
# @global DRY_RUN If set to "true", performs a dry-run without making changes.
#
# @param $1 The path to the daemon executable.
# @param $2 The ExecStart command for systemd.
# @param $3 Boolean flag ("true" or "false") to indicate if syslog should be used.
# @param $4 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if an invalid action is specified or an operation fails.
#
# @return Returns 0 on success, or 1 if any operation fails.
#
# @example
#   manage_service "/usr/bin/mydaemon" "/usr/local/bin/mydaemon -J" "false" "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
service_unit_path_is_mask() {
    local unit_path="${1:-}"
    local mask_target="${2:-/dev/null}"
    local link_target=""

    [[ -n "$unit_path" && -L "$unit_path" ]] || return 1
    link_target=$(readlink -- "$unit_path") || return 1
    [[ "$link_target" == "$mask_target" ]]
}

# -----------------------------------------------------------------------------
# @brief Return systemd's textual enablement state without treating a nonzero
#        status as loss of the state text.
# @details `systemctl is-enabled` intentionally exits nonzero for states such as
#          masked, disabled and not-found. Capturing the output separately keeps
#          `set -o pipefail` from hiding a reported mask.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
service_unit_enablement_state() {
    local unit_name="${1:-}"
    local state=""

    state=$(systemctl is-enabled "$unit_name" 2>/dev/null) || :
    printf '%s\n' "$state"
}

# -----------------------------------------------------------------------------
# @brief Verify that neither systemd nor either exact unit path reports a mask.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
verify_systemd_service_unmasked() {
    local unit_name="${1:-}"
    local persistent_path="${2:-}"
    local runtime_path="${3:-}"
    local mask_target="${4:-/dev/null}"
    local state=""

    state=$(service_unit_enablement_state "$unit_name")
    if [[ "$state" == "masked" || "$state" == "masked-runtime" ]]; then
        logE "Systemd service $unit_name remains $state after mask recovery."
        return 1
    fi
    if service_unit_path_is_mask "$persistent_path" "$mask_target"; then
        logE "Persistent systemd mask remains at $persistent_path."
        return 1
    fi
    if service_unit_path_is_mask "$runtime_path" "$mask_target"; then
        logE "Runtime systemd mask remains at $runtime_path."
        return 1
    fi
    return 0
}

# -----------------------------------------------------------------------------
# @brief Recover persistent or runtime masks before replacing a service unit.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
repair_systemd_service_mask() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local unit_name="${1:-}"
    local persistent_path="${2:-}"
    local runtime_path="${3:-}"
    local mask_target="${4:-/dev/null}"
    local state=""
    local persistent_mask=false
    local runtime_mask=false

    state=$(service_unit_enablement_state "$unit_name")
    if [[ "$state" == "masked" ]] ||
        service_unit_path_is_mask "$persistent_path" "$mask_target"; then
        persistent_mask=true
    fi
    if [[ "$state" == "masked-runtime" ]] ||
        service_unit_path_is_mask "$runtime_path" "$mask_target"; then
        runtime_mask=true
    fi

    if [[ "$persistent_mask" != "true" && "$runtime_mask" != "true" ]]; then
        debug_end "$debug"
        return 0
    fi

    if [[ "$persistent_mask" == "true" ]]; then
        if ! exec_command "Remove persistent systemd service mask" \
            systemctl unmask "$unit_name" "$debug"; then
            logE "Unable to repair the persistent systemd mask for $unit_name; installation cannot safely replace the unit."
            debug_end "$debug"
            return 1
        fi
    fi
    if [[ "$runtime_mask" == "true" ]]; then
        if ! exec_command "Remove runtime systemd service mask" \
            systemctl unmask --runtime "$unit_name" "$debug"; then
            logE "Unable to repair the runtime systemd mask for $unit_name; installation cannot safely replace the unit."
            debug_end "$debug"
            return 1
        fi
    fi

    # A dry run deliberately leaves the observed mask in place.
    if [[ "$DRY_RUN" != "true" ]] &&
        ! verify_systemd_service_unmasked \
            "$unit_name" "$persistent_path" "$runtime_path" "$mask_target"; then
        logE "Systemd mask recovery for $unit_name did not remove every persistent and runtime mask."
        debug_end "$debug"
        return 1
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Render and atomically install a regular systemd unit without following
#        a destination symlink.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
install_systemd_service_unit() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local source_path="${1:-}"
    local service_path="${2:-}"
    local runtime_path="${3:-}"
    local unit_name="${4:-}"
    local semantic_version="${5:-}"
    local daemon_name="${6:-}"
    local exec_start="${7:-}"
    local syslog_identifier="${8:-}"
    local mask_target="${9:-/dev/null}"
    local staged_path=""

    if [[ ! -f "$source_path" || -L "$source_path" ]]; then
        logE "Systemd unit template is not a regular file: $source_path"
        debug_end "$debug"
        return 1
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        debug_print "Dry run: would render and atomically install $service_path without following destination symlinks." "$debug"
        debug_end "$debug"
        return 0
    fi

    staged_path=$(mktemp "${service_path}.tmp.XXXXXX") || {
        logE "Unable to create a same-directory temporary service unit for $service_path."
        debug_end "$debug"
        return 1
    }
    if [[ ! -f "$staged_path" || -L "$staged_path" ]]; then
        rm -f -- "$staged_path"
        logE "Temporary systemd unit is not a regular file: $staged_path"
        debug_end "$debug"
        return 1
    fi

    if ! exec_command "Stage systemd service unit" cp -- "$source_path" "$staged_path" "$debug" ||
        ! replace_string_in_script "$staged_path" "SEMANTIC_VERSION" "$semantic_version" "$debug" ||
        ! replace_string_in_script "$staged_path" "DAEMON_NAME" "$daemon_name" "$debug" ||
        ! replace_string_in_script "$staged_path" "EXEC_START" "$exec_start" "$debug" ||
        ! replace_string_in_script "$staged_path" "SYSLOG_IDENTIFIER" "$syslog_identifier" "$debug" ||
        ! exec_command "Change ownership on staged systemd unit" \
            chown root:root "$staged_path" "$debug" ||
        ! exec_command "Change permissions on staged systemd unit" \
            chmod 0644 "$staged_path" "$debug"; then
        rm -f -- "$staged_path"
        logE "Unable to render a safe replacement unit for $unit_name."
        debug_end "$debug"
        return 1
    fi

    if [[ ! -f "$staged_path" || -L "$staged_path" ]]; then
        rm -f -- "$staged_path"
        logE "Rendered systemd unit is not a regular file: $staged_path"
        debug_end "$debug"
        return 1
    fi
    if ! verify_systemd_service_unmasked \
        "$unit_name" "$service_path" "$runtime_path" "$mask_target"; then
        rm -f -- "$staged_path"
        logE "Refusing to replace $unit_name because a mask reappeared during unit rendering."
        debug_end "$debug"
        return 1
    fi
    if ! exec_command "Atomically install systemd service unit" \
        mv -Tf -- "$staged_path" "$service_path" "$debug"; then
        rm -f -- "$staged_path"
        logE "Unable to atomically install $service_path."
        debug_end "$debug"
        return 1
    fi
    staged_path=""

    if [[ ! -f "$service_path" || -L "$service_path" ]]; then
        logE "Installed systemd unit is not a regular file: $service_path"
        debug_end "$debug"
        return 1
    fi

    debug_end "$debug"
    return 0
}

# shellcheck disable=SC2317
# shellcheck disable=SC2329
manage_service() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Ensure all three arguments are provided
    if [[ -z "$1" || -z "$2" || -z "$3" ]]; then
        logE "Error: Missing required arguments."
        logE "Usage: manage_service <daemon_exe> <exec_start>"
        debug_end "$debug"
        return 1
    fi

    # Get from args
    # shellcheck disable=SC2034
    local daemon_exe="$1"
    local exec_start="$2"

    # Declare local variables
    local daemon_name
    local source_path
    local daemon_systemd_name
    local service_path
    local runtime_service_path
    local syslog_identifier
    local service_state=""
    local semantic_version=""
    local retval=0

    daemon_name="${WSPR_SERVICE}" # Remove path
    source_path="$LOCAL_SYSTEMD_DIR/generic.service"
    daemon_systemd_name="${daemon_name}.service"
    # Install under /etc so it overrides anything in /lib
    service_path="/etc/systemd/system/${daemon_systemd_name}"
    runtime_service_path="/run/systemd/system/${daemon_systemd_name}"
    syslog_identifier="$daemon_name"       # Use stripped daemon_exe

    if [[ "$ACTION" == "install" ]]; then
        if [[ ! -f "$source_path" || -L "$source_path" ]]; then
            logE "Systemd unit template is not a regular file: $source_path"
            debug_end "$debug"
            return 1
        fi
        semantic_version="$SEM_VER"
        if [[ -z "$semantic_version" ]]; then
            logE "Unable to resolve the version for $daemon_systemd_name."
            debug_end "$debug"
            return 1
        fi
        repair_systemd_service_mask \
            "$daemon_systemd_name" "$service_path" "$runtime_service_path" "/dev/null" "$debug" || {
            debug_end "$debug"
            return 1
        }

        if [[ "$DRY_RUN" != "true" ]]; then
            if systemctl is-active --quiet "$daemon_systemd_name" 2>/dev/null; then
                exec_command "Stop systemd service" \
                    systemctl stop "$daemon_systemd_name" "$debug" || {
                    debug_end "$debug"
                    return 1
                }
            fi
            service_state=$(service_unit_enablement_state "$daemon_systemd_name")
            case "$service_state" in
                enabled|enabled-runtime|linked|linked-runtime|alias)
                    exec_command "Disable systemd service" \
                        systemctl disable "$daemon_systemd_name" "$debug" || {
                        debug_end "$debug"
                        return 1
                    }
                    ;;
            esac
        fi

        install_systemd_service_unit \
            "$source_path" "$service_path" "$runtime_service_path" \
            "$daemon_systemd_name" "$semantic_version" "$daemon_name" \
            "$exec_start" "$syslog_identifier" "/dev/null" "$debug" || {
            debug_end "$debug"
            return 1
        }
        exec_command "Reload systemd" systemctl daemon-reload "$debug" || {
            debug_end "$debug"
            return 1
        }
        exec_command "Enable systemd service" \
            systemctl enable "$daemon_systemd_name" "$debug" || {
            debug_end "$debug"
            return 1
        }
        exec_command "Start systemd service" \
            systemctl restart "$daemon_systemd_name" "$debug" || {
            debug_end "$debug"
            return 1
        }

    elif [[ "$ACTION" == "uninstall" ]]; then
        if [[ -e "$service_path" || -L "$service_path" ]]; then
            if systemctl is-active --quiet "$daemon_systemd_name" 2>/dev/null; then
                exec_command "Stop systemd service" systemctl stop "${daemon_systemd_name}" "$debug" || retval=1
            fi

            service_state=$(service_unit_enablement_state "$daemon_systemd_name")
            case "$service_state" in
                enabled|enabled-runtime|linked|linked-runtime|alias)
                    exec_command "Disable systemd service" systemctl disable "${daemon_systemd_name}" "$debug" || retval=1
                    ;;
            esac

            exec_command "Remove service file" rm -f "${service_path}" "$debug" || retval=1
            if [[ "$DRY_RUN" != "true" ]]; then
                exec_command "Reload systemd" systemctl daemon-reload "$debug" || retval=1
            fi
        else
            debug_print "Service file $service_path does not exist. Skipping removal." "$debug"
        fi
    else
        die 1 "Invalid action. Use 'install' or 'uninstall'."
    fi

    debug_end "$debug"
    return "$retval"
}

# -----------------------------------------------------------------------------
# @brief Resolve the REST and WebSocket ports used by the installed service.
# @details Mirrors the application's valid unprivileged port range and default
#          fallback so Apache and readiness checks follow preserved INI values.
#          Results are returned in WSPRRYPI_WEB_PORT and WSPRRYPI_SOCKET_PORT.
# -----------------------------------------------------------------------------
resolve_wsprrypi_service_ports() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local ini_path="${1:-/usr/local/etc/${WSPR_INI}}"
    local configured_web="" configured_socket="" resolved=""
    WSPRRYPI_WEB_PORT=31415
    WSPRRYPI_SOCKET_PORT=31416

    if [[ -r "$ini_path" ]]; then
        resolved=$(awk '
            function trim(value) {
                gsub(/^[ \t]+|[ \t]+$/, "", value)
                return value
            }
            /^[[:space:]]*[;#]/ || /^[[:space:]]*$/ { next }
            /^[[:space:]]*\[/ {
                section = tolower($0)
                sub(/^[[:space:]]*\[/, "", section)
                sub(/\][[:space:]]*$/, "", section)
                section = trim(section)
                next
            }
            section == "operation" && index($0, "=") {
                line = $0
                sub(/[;#].*$/, "", line)
                key = line
                sub(/=.*/, "", key)
                key = tolower(trim(key))
                value = line
                sub(/^[^=]*=/, "", value)
                value = trim(value)
                if (key == "web port") web = value
                if (key == "socket port") socket = value
            }
            END { printf "%s|%s", web, socket }
        ' "$ini_path")
        IFS='|' read -r configured_web configured_socket <<<"$resolved"
    fi

    if [[ "$configured_web" =~ ^[0-9]+$ ]] && \
        ((10#$configured_web >= 1024 && 10#$configured_web <= 49151)); then
        WSPRRYPI_WEB_PORT=$((10#$configured_web))
    fi
    if [[ "$configured_socket" =~ ^[0-9]+$ ]] && \
        ((10#$configured_socket >= 1024 && 10#$configured_socket <= 49151)); then
        WSPRRYPI_SOCKET_PORT=$((10#$configured_socket))
    fi

    debug_print "Effective service ports: REST=${WSPRRYPI_WEB_PORT}, WebSocket=${WSPRRYPI_SOCKET_PORT}." "$debug"
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Prove the installed application is actually available before success.
# @details A successful systemctl start job can still mean that systemd skipped
#          the service because a unit condition was false. Poll the resulting
#          active state and configured direct REST endpoint, then, when web mode
#          is enabled, the installed Apache proxy endpoint. This is application
#          readiness only; it does not exercise GPIO, clocks, transmission, or
#          RF.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
validate_wsprrypi_runtime() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    [[ "$ACTION" == "install" ]] || return 0
    if [[ "$DRY_RUN" == "true" ]]; then
        logD "Validate WsprryPi service and local API readiness (dry-run)."
        debug_end "$debug"
        return 0
    fi

    local attempts_remaining=30 service_ready=false direct_api_ready=false
    local proxy_api_ready=false
    local installed_ini="/usr/local/etc/${WSPR_INI}"
    resolve_wsprrypi_service_ports "$installed_ini" "$debug"

    while ((attempts_remaining-- > 0)); do
        if systemctl is-active --quiet "${WSPR_SERVICE}.service" 2>/dev/null; then
            service_ready=true
            if [[ "${NO_WEB:-false}" == "true" ]]; then
                direct_api_ready=true
                proxy_api_ready=true
                break
            fi
            if curl --fail --silent --show-error --max-time 2 \
                "http://127.0.0.1:${WSPRRYPI_WEB_PORT}/version" \
                >/dev/null 2>&1; then
                direct_api_ready=true
                if curl --fail --silent --show-error --max-time 2 \
                    "http://127.0.0.1/${REPO_NAME,,}/version" \
                    >/dev/null 2>&1; then
                    proxy_api_ready=true
                    break
                fi
            fi
        else
            service_ready=false
        fi
        sleep 1
    done

    if [[ "$service_ready" != "true" ]]; then
        warn "WsprryPi service did not become active; inspect systemd conditions and the service journal."
        debug_end "$debug"
        return 1
    fi
    if [[ "$direct_api_ready" != "true" ]]; then
        warn "WsprryPi service is active, but its configured local REST endpoint on port ${WSPRRYPI_WEB_PORT} did not become ready; inspect the installed INI and service journal."
        debug_end "$debug"
        return 1
    fi
    if [[ "$proxy_api_ready" != "true" ]]; then
        warn "WsprryPi REST endpoint on port ${WSPRRYPI_WEB_PORT} is ready, but the installed local /${REPO_NAME,,}/version Apache proxy did not become ready; inspect the active Apache proxy configuration."
        debug_end "$debug"
        return 1
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Manages the installation or removal of web files.
# @details This function installs or removes web-related files from the
#          `$LOCAL_WWW_DIR` source directory to `/var/www/html/$REPO_NAME`.
#          During installation, it ensures the target directory exists, copies
#          the files, and sets appropriate ownership and permissions. During
#          uninstallation, it removes the web directory.
#
# @global ACTION Specifies whether the function runs in 'install' or 'uninstall' mode.
# @global LOCAL_WWW_DIR The source directory containing web files.
# @global REPO_NAME The name of the repository, used for the web directory.
# @global DRY_RUN If set to "true", performs a dry-run without making changes.
#
# @param $1 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if required directories are missing or an operation fails.
#
# @return Returns 0 on success, or 1 if any operation fails.
#
# @example
#   manage_web "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
manage_web() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local source_path="$LOCAL_WWW_DIR"
    local target_path="/var/www/html/${REPO_NAME,,}"
    local retval=0 # Initialize return value
    local publisher="${LOCAL_REPO_DIR}/scripts/copy_ui.py"
    local source_commit=""
    local publisher_args=()

    if [[ "$ACTION" == "install" ]]; then
        # Validate source directory exists
        if [[ ! -d "$source_path" ]]; then
            logE "Error: Source directory '$source_path' not found."
            debug_end "$debug"
            return 1
        fi

        if [[ ! -x "$publisher" ]]; then
            logE "Error: UI publisher '$publisher' is missing or not executable."
            debug_end "$debug"
            return 1
        fi

        source_commit=$(git -C "$LOCAL_REPO_DIR" rev-parse HEAD 2>/dev/null) || {
            logE "Error: Unable to determine the exact UI source commit."
            debug_end "$debug"
            return 1
        }

        UI_PUBLICATION_RESULT_FILE="/run/wsprrypi-ui-publication-${$}.json"
        if [[ "$DRY_RUN" != "true" ]]; then
            UI_PUBLICATION_ATTEMPTED="true"
            rm -f "$UI_PUBLICATION_RESULT_FILE"
        fi
        publisher_args=(
            --source "$source_path"
            --target "$target_path"
            --source-commit "$source_commit"
            --application-version "$SEM_VER"
            --owner www-data
            --group www-data
            --result-file "$UI_PUBLICATION_RESULT_FILE"
        )
        if [[ "$FAIL_ON_UI_MODIFICATIONS" == "true" ]]; then
            publisher_args+=(--fail-on-ui-modifications)
        fi

        debug_print "Staging, validating, and publishing web files from '$source_path' to '$target_path'." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: $publisher ${publisher_args[*]}"
        else
            exec_command "Publish validated UI artifact" \
                "$publisher" \
                "${publisher_args[@]}" \
                "$debug" || retval=1
            if [[ "$retval" -eq 0 ]]; then
                exec_command "Set log permissions" \
                    usermod -aG systemd-journal www-data \
                    "$debug" || retval=1
            fi
        fi

    elif [[ "$ACTION" == "uninstall" ]]; then
        # Remove the web directory
        debug_print "Removing web directory '$target_path'." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: rm -rf $target_path"
        else
            exec_command "Remove web directory" rm -rf "${target_path}" "$debug" || retval=1
        fi
    else
        die 1 "Invalid action. Use 'install' or 'uninstall'."
    fi

    debug_end "$debug"
    return "$retval"
}

# -----------------------------------------------------------------------------
# @brief Manages the legacy-Pi sound module for WsprryPi.
# @details On pre-Pi-5 models, installation blacklists `snd_bcm2835` to avoid
#          conflicts with WsprryPi's legacy clock use, and uninstallation
#          removes that entry. Pi 5 and Compute Module 5 never create the
#          blacklist; uninstall only cleans up an obsolete entry left by an
#          older WsprryPi installation.
#
# @global ACTION Specifies whether the function runs in 'install' or 'uninstall' mode.
# @global REBOOT Indicates whether a system reboot is required.
#
# @param $1 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if an invalid action is specified.
#
# @return Returns 0 on success.
#
# @example
#   manage_sound "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
manage_sound() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local blacklist
    local file
    local line_count

    blacklist="blacklist snd_bcm2835"
    file="/etc/modprobe.d/alsa-blacklist.conf"

    # Pi 5 has no bcm2835 analogue-audio device sharing the legacy clock
    # peripheral. Never create this blacklist there. During uninstall, still
    # remove an entry left by an older WsprryPi installation, but that cleanup
    # does not require a reboot or mean that Pi 5 sound was re-enabled.
    local sound_reboot_required="true"
    if is_pi5; then
        if [[ "$ACTION" == "install" ]]; then
            debug_print "Legacy snd_bcm2835 sound management is not applicable on Raspberry Pi 5." "$debug"
            debug_end "$debug"
            return 0
        fi
        sound_reboot_required="false"
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        if [[ "$ACTION" == "install" ]]; then
            if [[ ! -f "$file" ]]; then
                logD "Exec: create $file with '$blacklist'"
                REBOOT="true"
            elif ! grep -Fxq "$blacklist" "$file"; then
                logD "Exec: append '$blacklist' to $file"
                REBOOT="true"
            else
                debug_print "Sound is already disabled." "$debug"
            fi
        elif [[ "$ACTION" == "uninstall" ]]; then
            if [[ -f "$file" ]] && grep -Fxq "$blacklist" "$file"; then
                logD "Exec: remove '$blacklist' from $file"
                if [[ "$sound_reboot_required" == "true" ]]; then
                    REBOOT="true"
                fi
            else
                debug_print "Sound is already enabled or no blacklist file exists." "$debug"
            fi
        else
            die 1 "Invalid action. Use 'install' or 'uninstall'."
        fi
        debug_end "$debug"
        return 0
    fi

    if [[ "$ACTION" == "install" ]]; then
        if [[ ! -f "$file" ]]; then
            # Create file and add the blacklist line
            exec_command "Disable legacy analogue audio" tee -- "$file" "$debug" <<<"$blacklist" || return 1
            REBOOT="true"
            debug_print "Created $file and disabled sound." "$debug"
        elif ! grep -Fxq "$blacklist" "$file"; then
            # Append blacklist line if it doesn't exist
            exec_command "Disable legacy analogue audio" tee -a -- "$file" "$debug" <<<"$blacklist" || return 1
            REBOOT="true"
            debug_print "Added blacklist entry to $file." "$debug"
        else
            debug_print "Sound is already disabled." "$debug"
        fi

    elif [[ "$ACTION" == "uninstall" ]]; then
        if [[ -f "$file" ]] && grep -Fxq "$blacklist" "$file"; then
            line_count=$(wc -l <"$file")

            if [[ "$line_count" -eq 1 ]]; then
                # If it's the only line, delete the file
                exec_command "Remove legacy audio blacklist file" rm -f "$file" "$debug" || return 1
                if [[ "$sound_reboot_required" == "true" ]]; then
                    debug_print "Removed $file and re-enabled sound." "$debug"
                else
                    debug_print "Removed obsolete Pi 5 sound blacklist file $file." "$debug"
                fi
            else
                # Otherwise, remove just the blacklist line
                exec_command "Remove legacy audio blacklist entry" \
                    sed -i "\|$blacklist|d" "$file" "$debug" || return 1
                debug_print "Removed blacklist entry from $file." "$debug"
            fi
            if [[ "$sound_reboot_required" == "true" ]]; then
                REBOOT="true"
            fi
        else
            debug_print "Sound is already enabled or no blacklist file exists." "$debug"
        fi
    else
        die 1 "Invalid action. Use 'install' or 'uninstall'."
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Enable Raspberry Pi I2C support during installation.
# @details Uses raspi-config when available, otherwise updates the active boot
#          config file directly and ensures `dtparam=i2c_arm=on` exists exactly
#          once. The change is idempotent and requests a reboot only when the
#          script makes a boot-configuration change.
#
# @global ACTION Specifies whether the function runs in 'install' or 'uninstall' mode.
# @global REBOOT Indicates whether a system reboot is required.
#
# @param $1 Debug flag for enabling or disabling debug output.
#
# @return Returns 0 on success.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
manage_i2c() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if [[ "$ACTION" != "install" ]]; then
        debug_end "$debug"
        return 0
    fi

    local boot_config=""
    local i2c_setting="dtparam=i2c_arm=on"

    if [[ -f "/boot/firmware/config.txt" ]]; then
        boot_config="/boot/firmware/config.txt"
    elif [[ -f "/boot/config.txt" ]]; then
        boot_config="/boot/config.txt"
    else
        debug_end "$debug"
        die 1 "Could not locate Raspberry Pi boot config file."
    fi

    if grep -Eq '^[[:space:]]*dtparam=i2c_arm=on([[:space:]]*(#.*)?)?$' "$boot_config"; then
        debug_print "I2C already enabled in $boot_config." "$debug"
        debug_end "$debug"
        return 0
    fi

    if command -v raspi-config >/dev/null 2>&1; then
        if exec_command "Enable I2C" raspi-config nonint do_i2c 0 "$debug"; then
            debug_end "$debug"
            return 0
        fi
    fi

    if grep -Eq '^[[:space:]#]*dtparam=i2c_arm=' "$boot_config"; then
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: sed -i 's|^[[:space:]#]*dtparam=i2c_arm=.*|$i2c_setting|' $boot_config"
        else
            exec_command "Enable I2C in boot configuration" \
                sed -i "s|^[[:space:]#]*dtparam=i2c_arm=.*|$i2c_setting|" "$boot_config" "$debug" || return 1
        fi
    else
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: printf '\\n%s\\n' '$i2c_setting' >> $boot_config"
        else
            exec_command "Enable I2C in boot configuration" \
                tee -a -- "$boot_config" "$debug" <<<$'\n'"$i2c_setting" || return 1
        fi
    fi

    REBOOT="true"
    debug_print "Enabled I2C in $boot_config." "$debug"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief    Install or uninstall all Apache bits for WsprryPi
# @details
#   * On install (ACTION=install or empty):
#     – Adds DEFAULT_SERVERNAME to $APACHE_CONF if missing
#     – Writes the single $DEFAULT_SITES_CONF/wsprrypi.conf with:
#         • A redirect “/ → /wsprrypi/” (if stock Apache home is detected)
#         • REST proxies using the effective configured Web Port
#         • WS proxy using the effective configured Socket Port
#         • <Proxy> blocks allowing remote access
#     – Disables 000-default.conf, enables wsprrypi.conf
#   * On uninstall (ACTION=uninstall):
#     – Disables wsprrypi.conf, re-enables 000-default.conf
#     – Deletes wsprrypi.conf
#     – Removes the ServerName line from $APACHE_CONF
#   In all cases it runs configtest + reload.
#
# @global ACTION             install|uninstall (empty→install)
# @global APACHE_CONF        path to apache2.conf
# @global DEFAULT_SITES_CONF path to sites-available
# @global DEFAULT_SERVERNAME the literal “ServerName …” line
# @param  $@                 optional debug flags
# @return 0 on success, non-zero on failure
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
manage_apache() {
    # Start debug and filter parameters
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables after debug initialization
    local status=0
    local config_file source_path site_conf server_name target_conf
    local network_policy_file network_policy_renderer installed_ini

    config_file="wsprrypi.conf"
    source_path="${LOCAL_CONFIG_DIR}/${config_file}"
    site_conf="${DEFAULT_SITES_CONF}/${config_file}"
    server_name="${DEFAULT_SERVERNAME}"
    network_policy_file="/usr/local/etc/wsprrypi-apache-network-policy.conf"
    network_policy_renderer="${LOCAL_REPO_DIR}/scripts/render-apache-privileged-network-policy.py"
    installed_ini="/usr/local/etc/${WSPR_INI}"

    if [[ -z "$ACTION" || "$ACTION" == install ]]; then
        if [[ ! -f "$network_policy_renderer" ]]; then
            logE "Network policy renderer not found at '$network_policy_renderer'."
            debug_end "$debug"
            return 1
        fi
        exec_command "Render Apache privileged-network policy" \
            python3 "$network_policy_renderer" \
            --ini "$installed_ini" \
            --output "$network_policy_file" \
            "$debug" || {
            logE "Failed to render the Apache privileged-network policy."
            debug_end "$debug"
            return 1
        }
        # Ensure ServerName is present in the main Apache config
        if [[ "${APACHE_WEB_MODE:-unknown}" == "unknown" ]]; then
            detect_apache_web_mode "$debug" || true
        fi

        if ! grep -qF "$server_name" "$APACHE_CONF"; then
            exec_command "Insert ServerName directive" \
                sed -i "1i $server_name" "$APACHE_CONF" \
                "$debug"
        fi

        # Preserve existing unrelated custom sites by installing only the
        # WsprryPi UI files and adding the required proxy rules to the
        # selected active vhost configs.
        if [[ "${APACHE_WEB_MODE:-unknown}" == "custom_site" ]]; then

            exec_command "Enable proxy modules" \
                a2enmod headers proxy proxy_http proxy_wstunnel \
                "$debug" || {
                logE "Failed to enable proxy modules."
                debug_end "$debug"
                return 1
            }

            if ! find_active_apache_site_confs "$debug"; then
                logW "No active Apache vhost config was found to amend."
                logW "WsprryPi files were copied, but Apache routing was not changed."
                debug_end "$debug"
                return 0
            fi

            for target_conf in "${APACHE_ACTIVE_SITE_CONFS[@]}"; do
                if ! install_wsprrypi_proxy_block "$target_conf" "$installed_ini" "$debug"; then
                    logE "Failed to update Apache vhost '$target_conf'."
                    debug_end "$debug"
                    return 1
                fi
            done

            exec_command "Test Apache configuration" apache2ctl configtest "$debug" || {
                logE "Apache test failed."
                debug_end "$debug"
                return 1
            }

            exec_command "Reload Apache" systemctl reload apache2 "$debug" || {
                logE "Failed to reload Apache."
                debug_end "$debug"
                return 1
            }

            debug_end "$debug"
            return 0
        fi

        # Copy vhost config to sites-available
        debug_print "Copying vhost from $source_path → $site_conf" "$debug"
        if [[ ! -f "$source_path" ]]; then
            logE "Config file not found at '$source_path'"
            debug_end "$debug"
            return 1
        fi

        exec_command "Copy Apache vhost to sites-available" cp "$source_path" "$site_conf" "$debug" || {
            logE "Failed to copy '$source_path' to '$site_conf'"
            debug_end "$debug"
            return 1
        }

        if ! install_wsprrypi_proxy_block "$site_conf" "$installed_ini" "$debug"; then
            logE "Failed to configure WsprryPi service ports in '$site_conf'."
            debug_end "$debug"
            return 1
        fi

        # If stock page, comment out log lines in the vhost before enabling.
        if [[ "${APACHE_WEB_MODE:-unknown}" == "stock" ]] && is_stock_apache_page "/var/www/html/index.html" "$debug"; then
            modify_comment_lines "${site_conf}" ".log" comment "$debug"
        fi

        # Enable required modules
        exec_command "Enable proxy modules" a2enmod headers proxy proxy_http proxy_wstunnel "$debug" || {
            logE "Failed to enable proxy modules."
            debug_end "$debug"
            return 1
        }

        # Disable the stock Apache default site only when the install is
        # stock/default-owned or WsprryPi-owned.
        if [[ "${APACHE_WEB_MODE:-unknown}" != "custom_site" ]]; then
            exec_command "Disable default site" a2dissite 000-default.conf "$debug" || {
                logE "Failed to disable default site."
                debug_end "$debug"
                return 1
            }
        fi

        # Enable wsprrypi site
        exec_command "Enable wsprrypi site" a2ensite wsprrypi.conf "$debug" || {
            logE "Failed to enablw wsprrypi site."
            debug_end "$debug"
            return 1
        }

    else
        # Uninstall path. If WsprryPi owns the active vhost, restore the
        # default Apache site. If it was integrated into an existing site,
        # remove only the managed proxy block and leave that site enabled.
        if [[ -e "/etc/apache2/sites-enabled/wsprrypi.conf" ]]; then
            exec_command "Disable wsprrypi site" a2dissite wsprrypi.conf "$debug"

            exec_command "Enable default site" a2ensite 000-default.conf "$debug"
        elif find_active_apache_site_confs "$debug"; then
            for target_conf in "${APACHE_ACTIVE_SITE_CONFS[@]}"; do
                remove_wsprrypi_proxy_block "$target_conf" "$debug" || {
                    logE "Failed to remove WsprryPi proxy block from '$target_conf'."
                    debug_end "$debug"
                    return 1
                }
            done
        fi

        exec_command "Remove wsprrypi available config" rm -f "${site_conf}" "$debug"

        exec_command "Remove Apache privileged-network policy" \
            rm -f "$network_policy_file" "$debug"

        exec_command "Remove ServerName directive" sed -i "/^${server_name}/d" "$APACHE_CONF" "$debug"
    fi

    # Test and reload Apache
    exec_command "Test Apache configuration" apache2ctl configtest "$debug" || {
        logE "Apache test failed."
        debug_end "$debug"
        return 1
    }

    exec_command "Reload Apache" systemctl reload apache2 "$debug" || {
        logE "Failed to reload Apache."
        debug_end "$debug"
        return 1
    }

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief    Check if an HTML file is the stock Apache landing page.
# @param    $1  Path to the file to check (defaults to /var/www/html/index.html).
# @return   0 if it looks like the stock Apache page, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
is_stock_apache_page() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local file="${1:-/var/www/html/index.html}"

    # Must exist and be readable.
    if [[ ! -r "$file" ]]; then
        debug_end "$debug"
        return 1
    fi

    # Common stock-page phrases (Ubuntu/Debian, RHEL/CentOS, generic)
    if grep -qiE \
        'It works!|Apache2 (Ubuntu|Debian) Default Page|If you see this page, the Apache HTTP Server must be installed correctly' \
        "$file"; then
        debug_end "$debug"
        return 0
    else
        debug_end "$debug"
        return 1
    fi
}

# -----------------------------------------------------------------------------
# @brief Check whether Apache document root contains a custom website.
# @details Returns success when /var/www/html contains files that do not
#          appear to be only the default Apache landing page.
#
# @param $1 Optional debug flag.
#
# @return 0 if custom content is detected, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
has_custom_apache_webroot() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local webroot="/var/www/html"
    local default_index="${webroot}/index.html"

    if [[ ! -d "$webroot" ]]; then
        debug_end "$debug"
        return 1
    fi

    if [[ ! -f "$default_index" ]]; then
        debug_print "Apache webroot has no root index.html." "$debug"
        debug_end "$debug"
        return 1
    fi

    if is_stock_apache_page "$default_index" "$debug"; then
        debug_print "Apache root index.html is the stock Apache page." "$debug"
        debug_end "$debug"
        return 1
    fi

    debug_print "Apache root index.html appears to be custom content." "$debug"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Check whether the installed webroot already contains WsprryPi UI files.
# @details Detects a prior WsprryPi web install in /var/www/html/wsprrypi by
#          looking for recognizable UI files and directories.
#
# @param $1 Optional debug flag.
#
# @return 0 if a WsprryPi webroot is detected, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
wsprrypi_webroot_present() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local webroot="/var/www/html/wsprrypi"

    if [[ -f "${webroot}/index.php" && -f "${webroot}/site.js" && -d "${webroot}/views" ]]; then
        debug_print "Detected existing WsprryPi webroot at ${webroot}." "$debug"
        debug_end "$debug"
        return 0
    fi

    debug_end "$debug"
    return 1
}

# -----------------------------------------------------------------------------
# @brief Check whether an Apache config contains WsprryPi-managed routing.
# @details Detects either the managed BEGIN/END marker block or explicit
#          /wsprrypi proxy routes that indicate prior WsprryPi integration.
#
# @param $1 Apache config path.
# @param $2 Optional debug flag.
#
# @return 0 if WsprryPi routing markers are present, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
apache_config_contains_wsprrypi_routes() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local config_path="${1:-}"

    if [[ -z "$config_path" || ! -f "$config_path" ]]; then
        debug_end "$debug"
        return 1
    fi

    if grep -qE \
        '# BEGIN WsprryPi proxy configuration|/wsprrypi/config|/wsprrypi/version|/wsprrypi/socket|wsprrypi\.conf' \
        "$config_path"; then
        debug_print "Detected WsprryPi Apache routing in ${config_path}." "$debug"
        debug_end "$debug"
        return 0
    fi

    debug_end "$debug"
    return 1
}

# -----------------------------------------------------------------------------
# @brief Check whether Apache already has a WsprryPi-managed installation.
# @details Returns success if any of the expected WsprryPi Apache ownership
#          signals are present: wsprrypi.conf in sites-available, wsprrypi.conf
#          enabled, an existing WsprryPi webroot, or existing WsprryPi proxy
#          routing markers in Apache config files.
#
# @param $1 Optional debug flag.
#
# @return 0 if an existing WsprryPi install is detected, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
existing_wsprrypi_apache_install_detected() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local enabled_entry
    local resolved_entry
    local apache_config

    if [[ -f "/etc/apache2/sites-available/wsprrypi.conf" ]]; then
        debug_print "Detected existing /etc/apache2/sites-available/wsprrypi.conf." "$debug"
        debug_end "$debug"
        return 0
    fi

    if [[ -e "/etc/apache2/sites-enabled/wsprrypi.conf" ]]; then
        debug_print "Detected existing /etc/apache2/sites-enabled/wsprrypi.conf." "$debug"
        debug_end "$debug"
        return 0
    fi

    for enabled_entry in /etc/apache2/sites-enabled/*.conf; do
        [[ -e "$enabled_entry" ]] || continue
        resolved_entry=$(readlink -f "$enabled_entry")
        if [[ "$(basename "$resolved_entry")" == "wsprrypi.conf" ]]; then
            debug_print "Detected enabled WsprryPi Apache site via ${enabled_entry}." "$debug"
            debug_end "$debug"
            return 0
        fi
    done

    if wsprrypi_webroot_present "$debug"; then
        debug_end "$debug"
        return 0
    fi

    for apache_config in /etc/apache2/sites-available/*.conf /etc/apache2/sites-enabled/*.conf; do
        [[ -e "$apache_config" ]] || continue
        resolved_entry=$(readlink -f "$apache_config")
        [[ -f "$resolved_entry" ]] || continue
        if apache_config_contains_wsprrypi_routes "$resolved_entry" "$debug"; then
            debug_end "$debug"
            return 0
        fi
    done

    debug_end "$debug"
    return 1
}

# -----------------------------------------------------------------------------
# @brief Detect and record the Apache web integration mode.
# @details Determines whether Apache is unavailable, currently serving a stock
#          site, already hosting a WsprryPi-owned install, or hosting an
#          unrelated custom site. Logs exactly one mode message during the
#          preflight phase and stores the result in APACHE_WEB_MODE.
#
# @param $1 Optional debug flag.
#
# @return 0 on successful detection, 1 when Apache is unavailable.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
detect_apache_web_mode() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if [[ "${APACHE_WEB_MODE_DETECTED:-false}" == "true" && "${APACHE_WEB_MODE:-unknown}" != "unknown" ]]; then
        debug_end "$debug"
        return 0
    fi

    APACHE_WEB_MODE="unknown"
    APACHE_WEB_MODE_DETECTED="false"

    if existing_wsprrypi_apache_install_detected "$debug"; then
        APACHE_WEB_MODE="existing_wsprrypi"
        APACHE_WEB_MODE_DETECTED="true"
        logI "Apache web mode: existing WsprryPi installation."
        debug_end "$debug"
        return 0
    fi

    if has_custom_apache_webroot "$debug"; then
        APACHE_WEB_MODE="custom_site"
        APACHE_WEB_MODE_DETECTED="true"
        logW "Apache web mode: existing non-WsprryPi website detected."
        logW "WsprryPi will be installed under /wsprrypi without disabling the existing site."
        debug_end "$debug"
        return 0
    fi

    APACHE_WEB_MODE="stock"
    APACHE_WEB_MODE_DETECTED="true"
    logI "Apache web mode: stock/default site."
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Validate Apache availability after package management.
# @details Confirms Apache tooling and expected config directories exist once
#          package installation has completed. Fails web-enabled installs when
#          Apache is still unavailable, but allows `--no-web` installs to
#          proceed.
#
# @param $1 Optional debug flag.
#
# @return 0 when Apache is available or web is disabled, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
validate_apache_web_availability() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if command -v apache2ctl >/dev/null 2>&1 && \
        [[ -d "/etc/apache2" ]] && \
        [[ -d "/etc/apache2/sites-available" ]]; then
        debug_end "$debug"
        return 0
    fi

    APACHE_WEB_MODE="no_apache"

    if [[ "${NO_WEB:-false}" != "true" ]]; then
        logE "Apache is not installed or not available; web UI cannot be configured."
        debug_end "$debug"
        return 1
    fi

    logI "Apache unavailable, but --no-web is set; continuing without web UI."
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Select a single preferred Apache vhost for a given port.
# @details Evaluates a list of enabled config files and returns success only
#          when exactly one file can be selected safely for the requested port.
#          Preference order is `*:port` first, then `_default_:port`.
#
# @param $1 Name of the array variable holding candidate config paths.
# @param $2 Target port (`80` or `443`).
# @param $3 Name of the output variable to receive the selected config path.
# @param $4 Optional debug flag.
#
# @return 0 when a single preferred config is found, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
select_preferred_apache_vhost_for_port() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local array_name="${1:-}"
    local port="${2:-}"
    local output_var="${3:-}"
    local pattern
    local resolved
    local -n candidates_ref="$array_name"
    local matches=()

    if [[ -z "$array_name" || -z "$port" || -z "$output_var" ]]; then
        debug_end "$debug"
        return 1
    fi

    for pattern in \
        "<VirtualHost[[:space:]]+\\*:${port}>" \
        "<VirtualHost[[:space:]]+_default_:${port}>"; do
        matches=()

        for resolved in "${candidates_ref[@]}"; do
            if grep -qiE "$pattern" "$resolved"; then
                matches+=("$resolved")
            fi
        done

        if (( ${#matches[@]} == 1 )); then
            printf -v "$output_var" '%s' "${matches[0]}"
            debug_end "$debug"
            return 0
        fi
    done

    debug_end "$debug"
    return 1
}


# -----------------------------------------------------------------------------
# @brief Locate active Apache vhost configs to amend.
# @details Sets APACHE_ACTIVE_SITE_CONFS to enabled Apache site configs that are
#          not the WsprryPi config. HTTP vhosts are included first, followed by
#          SSL vhosts, so an existing custom site can serve WsprryPi under both
#          http://host/wsprrypi/ and https://host/wsprrypi/ when HTTPS exists.
#
# @param $1 Optional debug flag.
#
# @return 0 when at least one config is found, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
find_active_apache_site_confs() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local enabled_dir="/etc/apache2/sites-enabled"
    local candidate
    local resolved
    local unique_enabled=()
    local selected_http=""
    local selected_ssl=""

    APACHE_ACTIVE_SITE_CONF=""
    APACHE_ACTIVE_SITE_CONFS=()

    if [[ ! -d "$enabled_dir" ]]; then
        debug_print "Apache sites-enabled directory was not found." "$debug"
        debug_end "$debug"
        return 1
    fi

    for candidate in "$enabled_dir"/*.conf; do
        [[ -e "$candidate" ]] || continue

        resolved=$(readlink -f "$candidate")

        if [[ "$(basename "$resolved")" == "wsprrypi.conf" ]]; then
            continue
        fi

        [[ -f "$resolved" ]] || continue

        if [[ " ${unique_enabled[*]} " != *" $resolved "* ]]; then
            unique_enabled+=("$resolved")
        fi
    done

    if (( ${#unique_enabled[@]} == 0 )); then
        debug_print "No suitable active Apache vhost was found." "$debug"
        debug_end "$debug"
        return 1
    fi

    if (( ${#unique_enabled[@]} == 1 )); then
        APACHE_ACTIVE_SITE_CONFS=("${unique_enabled[0]}")
        APACHE_ACTIVE_SITE_CONF="${APACHE_ACTIVE_SITE_CONFS[0]}"
        debug_print "Selected sole enabled Apache vhost: ${APACHE_ACTIVE_SITE_CONF}" "$debug"
        debug_end "$debug"
        return 0
    fi

    if select_preferred_apache_vhost_for_port unique_enabled 80 selected_http "$debug"; then
        if [[ -n "$selected_http" ]]; then
            APACHE_ACTIVE_SITE_CONFS+=("$selected_http")
            debug_print "Selected HTTP Apache vhost: ${selected_http}" "$debug"
        fi
    fi

    if select_preferred_apache_vhost_for_port unique_enabled 443 selected_ssl "$debug"; then
        if [[ -n "$selected_ssl" && " ${APACHE_ACTIVE_SITE_CONFS[*]} " != *" $selected_ssl "* ]]; then
            APACHE_ACTIVE_SITE_CONFS+=("$selected_ssl")
        fi
        if [[ -n "$selected_ssl" ]]; then
            debug_print "Selected HTTPS Apache vhost: ${selected_ssl}" "$debug"
        fi
    fi

    if (( ${#APACHE_ACTIVE_SITE_CONFS[@]} == 0 )); then
        debug_print "Multiple enabled Apache vhosts were found, but none matched a safe HTTP/HTTPS selection pattern." "$debug"
        debug_end "$debug"
        return 1
    fi

    APACHE_ACTIVE_SITE_CONF="${APACHE_ACTIVE_SITE_CONFS[0]}"

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Locate the first active Apache vhost config to amend.
# @details Compatibility wrapper for callers that expect APACHE_ACTIVE_SITE_CONF.
#
# @param $1 Optional debug flag.
#
# @return 0 when a config is found, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
find_active_apache_site_conf() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if find_active_apache_site_confs "$debug"; then
        debug_end "$debug"
        return 0
    fi

    debug_end "$debug"
    return 1
}

# -----------------------------------------------------------------------------
# @brief Add or update the WsprryPi proxy block in an Apache vhost.
# @details Removes any existing managed WsprryPi proxy block, then inserts the
#          current block before each closing VirtualHost tag. If no VirtualHost
#          tag is present, the block is appended to the file.
#
# @param $1 Apache vhost config path to update.
# @param $2 Effective installed INI path.
# @param $3 Optional debug flag.
#
# @return 0 on success, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
install_wsprrypi_proxy_block() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local config_file="${1:-}"
    local installed_ini="${2:-/usr/local/etc/${WSPR_INI}}"
    local tmp_file block_file
    local backup_file
    local proxy_block

    resolve_wsprrypi_service_ports "$installed_ini" "$debug"

    if [[ "$DRY_RUN" == "true" ]]; then
        logD "Exec: update managed WsprryPi proxy block in $config_file for REST port ${WSPRRYPI_WEB_PORT} and WebSocket port ${WSPRRYPI_SOCKET_PORT}"
        debug_end "$debug"
        return 0
    fi

    if [[ -z "$config_file" || ! -f "$config_file" ]]; then
        logE "Apache vhost config '$config_file' was not found."
        debug_end "$debug"
        return 1
    fi

    proxy_block=$(cat <<EOF
    # BEGIN WsprryPi proxy configuration
    # REST API (port ${WSPRRYPI_WEB_PORT})
    ProxyPass        /wsprrypi/config  http://127.0.0.1:${WSPRRYPI_WEB_PORT}/config
    ProxyPassReverse /wsprrypi/config  http://127.0.0.1:${WSPRRYPI_WEB_PORT}/config
    ProxyPass        /wsprrypi/version http://127.0.0.1:${WSPRRYPI_WEB_PORT}/version
    ProxyPassReverse /wsprrypi/version http://127.0.0.1:${WSPRRYPI_WEB_PORT}/version
    # Support bundle API family and signed private-intake availability.
    ProxyPass        /wsprrypi/api/support-bundles http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/support-bundles
    ProxyPassReverse /wsprrypi/api/support-bundles http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/support-bundles
    ProxyPass        /wsprrypi/api/support-intake http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/support-intake
    ProxyPassReverse /wsprrypi/api/support-intake http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/support-intake
    ProxyPass        /wsprrypi/api/network-safety http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/network-safety
    ProxyPassReverse /wsprrypi/api/network-safety http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/network-safety
    ProxyPass        /wsprrypi/api/rp1-gpclk-route http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/rp1-gpclk-route
    ProxyPassReverse /wsprrypi/api/rp1-gpclk-route http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/rp1-gpclk-route

    ProxyPass        /wsprrypi/api/v1 http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/v1
    ProxyPassReverse /wsprrypi/api/v1 http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/v1
    ProxyPass        /wsprrypi/api/wtp http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/wtp
    ProxyPassReverse /wsprrypi/api/wtp http://127.0.0.1:${WSPRRYPI_WEB_PORT}/api/wtp

    Include /usr/local/etc/wsprrypi-apache-network-policy.conf

    # WebSocket (port ${WSPRRYPI_SOCKET_PORT})
    ProxyPass        /wsprrypi/socket  ws://127.0.0.1:${WSPRRYPI_SOCKET_PORT}/socket
    ProxyPassReverse /wsprrypi/socket  ws://127.0.0.1:${WSPRRYPI_SOCKET_PORT}/socket

    <Proxy "http://127.0.0.1:${WSPRRYPI_WEB_PORT}/*">
        Require all granted
    </Proxy>
    <Proxy "ws://127.0.0.1:${WSPRRYPI_SOCKET_PORT}/*">
        Require all granted
    </Proxy>
    # END WsprryPi proxy configuration
EOF
)

    tmp_file=$(mktemp) || {
        logE "Failed to create temporary file."
        debug_end "$debug"
        return 1
    }
    block_file=$(mktemp) || {
        rm -f "$tmp_file"
        logE "Failed to create proxy-block temporary file."
        debug_end "$debug"
        return 1
    }
    printf '%s\n' "$proxy_block" >"$block_file" || {
        rm -f "$tmp_file" "$block_file"
        logE "Failed to stage the managed Apache proxy block."
        debug_end "$debug"
        return 1
    }

    backup_file="${config_file}.wsprrypi.bak"
    if [[ ! -f "$backup_file" ]]; then
        exec_command "Back up Apache proxy configuration" cp "$config_file" "$backup_file" "$debug" || {
            rm -f "$tmp_file" "$block_file"
            logE "Failed to back up '$config_file'."
            debug_end "$debug"
            return 1
        }
    fi

    exec_command "Stage Apache proxy configuration" cp -p "$config_file" "$tmp_file" "$debug" || {
        rm -f "$tmp_file" "$block_file"
        logE "Failed to preserve Apache config metadata."
        debug_end "$debug"
        return 1
    }

    awk -v block_file="$block_file" '
        function print_block(   line) {
            while ((getline line < block_file) > 0) print line
            close(block_file)
        }

        /^[[:space:]]*# BEGIN WsprryPi proxy configuration[[:space:]]*$/ {
            in_block = 1
            next
        }

        /^[[:space:]]*# END WsprryPi proxy configuration[[:space:]]*$/ {
            in_block = 0
            next
        }

        !in_block {
            if ($0 ~ /^[[:space:]]*<\/VirtualHost>[[:space:]]*$/) {
                print_block()
                inserted = 1
            }
            print
        }

        END {
            if (!inserted) {
                print ""
                print_block()
            }
        }
    ' "$config_file" >"$tmp_file" || {
        rm -f "$tmp_file" "$block_file"
        logE "Failed to build updated Apache config."
        debug_end "$debug"
        return 1
    }

    exec_command "Install Apache proxy configuration" mv "$tmp_file" "$config_file" "$debug" || {
        rm -f "$tmp_file" "$block_file"
        logE "Failed to install updated Apache config."
        debug_end "$debug"
        return 1
    }
    rm -f "$block_file"

    debug_print "Installed WsprryPi proxy block in $config_file." "$debug"
    debug_end "$debug"
    return 0
}


# -----------------------------------------------------------------------------
# @brief Remove the managed WsprryPi proxy block from an Apache vhost.
# @details Removes only the lines between the WsprryPi BEGIN and END markers.
#
# @param $1 Apache vhost config path to update.
# @param $2 Optional debug flag.
#
# @return 0 on success, 1 otherwise.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
remove_wsprrypi_proxy_block() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local config_file="${1:-}"
    local tmp_file

    if [[ -z "$config_file" || ! -f "$config_file" ]]; then
        logE "Apache vhost config '$config_file' was not found."
        debug_end "$debug"
        return 1
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        logD "Exec: remove managed WsprryPi proxy block from $config_file"
        debug_end "$debug"
        return 0
    fi

    tmp_file=$(mktemp) || {
        logE "Failed to create temporary file."
        debug_end "$debug"
        return 1
    }

    awk '
        /^[[:space:]]*# BEGIN WsprryPi proxy configuration[[:space:]]*$/ {
            in_block = 1
            next
        }

        /^[[:space:]]*# END WsprryPi proxy configuration[[:space:]]*$/ {
            in_block = 0
            next
        }

        !in_block {
            print
        }
    ' "$config_file" >"$tmp_file" || {
        rm -f "$tmp_file"
        logE "Failed to remove managed WsprryPi proxy block."
        debug_end "$debug"
        return 1
    }

    exec_command "Remove WsprryPi Apache proxy configuration" mv "$tmp_file" "$config_file" "$debug" || {
        rm -f "$tmp_file"
        logE "Failed to install updated Apache config."
        debug_end "$debug"
        return 1
    }

    debug_print "Removed WsprryPi proxy block from $config_file." "$debug"
    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Cleans up the local repository files after installation or uninstallation.
# @details This function removes the local repository directory unless the script
#          is being executed from within the repository itself, in which case it
#          skips cleanup. It respects dry-run mode to simulate deletion without
#          actually removing files.
#
# @global USER_HOME The user's home directory path.
# @global REPO_NAME The name of the repository.
# @global IS_REPO Indicates if the script is running inside a repository.
# @global LOCAL_REPO_DIR The expected repository directory path.
# @global DRY_RUN If set to "true", performs a dry-run without deleting files.
#
# @param $1 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if the target directory is empty or deletion fails.
#
# @return Returns 0 if cleanup succeeds or is skipped, 1 if cleanup fails.
#
# @example
#   cleanup_files_in_directories "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317  # Ignore unreachable-code warnings in this function
cleanup_files_in_directories() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local dest_root
    dest_root="$LOCAL_REPO_DIR"

    # Always cleanup merged INI if it exists
    if [[ -f "$dest_root/config/wsprrypi_merged.ini" ]]; then
        exec_command "Delete merged INI source" \
            rm -f "${dest_root}/config/wsprrypi_merged.ini" \
            "$debug" || {
            logE "Failed to delete local install files."
            debug_end "$debug"
            return 1
        }
    fi

    # Prevent deletion if running inside the repository
    if [[ "$IS_REPO" == "true" ]]; then
        debug_end "$debug"
        return 0
    else
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Delete local repo files (dry-run)."
            debug_end "$debug"
            return 0
        elif [[ -d "$dest_root" ]]; then
            # Delete the repository directory
            exec_command "Delete local repository" rm -fr "${dest_root}" "$debug" || {
                logE "Failed to delete local install files."
                debug_end "$debug"
                return 1
            }
        else
            logW "Unable to delete '$dest_root', not a directory."
        fi
    fi

    debug_end "$debug"
    return 0
}

restore_daemon_state() {
    if [[ "${BINARY_INSTALLED:-false}" == "recovery-failed" ]]; then
        logE "Executable recovery needs attention; automatic service restart is withheld."
        return 1
    fi
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables
    local daemon_name retval
    retval=0

    # Get from args
    daemon_name="${WSPR_SERVICE}"

    # Restore Daemon
    debug_print "Restoring daemon" "$debug"
    # If it was running before, and the service still exists, start it again
    if [[ "$WAS_RUNNING" == true ]] && \
    systemctl list-unit-files "${daemon_name}.service" &>/dev/null; then
        exec_command "Restart ${WSPR_SERVICE}" \
            systemctl start "${daemon_name}.service" "$debug" || {
                logE "Error: Unable to restart ${WSPR_SERVICE}."
                retval=1
            }
    fi

    debug_end "$debug"
    return "$retval"
}

# -----------------------------------------------------------------------------
# @brief Displays a reboot prompt if a system reboot is required.
# @details
#   Checks the global semaphore flag REBOOT. If it is set to "true",
#   prints a contextual message and prompts the user to reboot the system.
#   The message varies based on whether ACTION is "install" or another mode.
#   Reads a key press before continuing.
#
# @param ... Optional debugging flag; when "debug" is passed, debug messages
#            are enabled.
#
# @return Always returns 0.
# -----------------------------------------------------------------------------
flag_need_reboot() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if [[ "$REBOOT" == "true" ]]; then
        printf "\n*Important Note:*\n\n"
        if [[ "$ACTION" == "install" ]]; then
            printf "Wsprry Pi uses the same hardware as the sound system to generate\n"
            printf "radio frequencies. This soundcard has been disabled. You must\n"
            printf "reboot the Pi with the following command after install for this\n"
            printf "to take effect:\n\n"
        else
            printf "The sound system has been re-enabled. If you wish to use\n"
            printf "audio on the Raspberry Pi, you must reboot for changes\n"
            printf "to take effect:\n\n"
        fi
        printf "'sudo reboot'\n\n"

        if [[ "$DRY_RUN" == "true" ]]; then
            debug_end "$debug"
            return 0
        fi

        if [[ -t 0 ]]; then
            read -rp "Press any key to continue." || true
        fi
        echo
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
report_ui_publication_result() {
    if [[ "$UI_PUBLICATION_ATTEMPTED" != "true" ]]; then
        return 0
    fi
    if [[ -n "$UI_PUBLICATION_RESULT_FILE" && -f "$UI_PUBLICATION_RESULT_FILE" ]]; then
        python3 "${LOCAL_REPO_DIR}/scripts/copy_ui.py" \
            --render-result "$UI_PUBLICATION_RESULT_FILE" || true
    else
        printf "\nUI replacement issue\n"
        printf "  Publication details are unavailable because the UI publisher did not create its result file.\n"
        printf "  Confirm the installed UI state before retrying.\n"
    fi
}

# @brief Finalizes the installation or uninstallation process.
# @details Logs and displays a completion message based on whether the install
#          or uninstall was successful or failed. It also provides follow-up
#          instructions if applicable.
#
# @global ACTION Specifies whether the function runs in 'install' or 'uninstall' mode.
# @global REPO_TITLE The display name of the repository.
# @global WSPR_SERVICE The executable name for WsprryPi.
#
# @param $1 The overall success or failure status (0 for success, 1 for failure).
# @param $2 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if an invalid action is specified.
#
# @return Returns 0 on success, or 1 if the process failed.
#
# @example
#   finish_script 0 "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317  # Ignore unreachable-code warnings in this function
finish_script() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Capture overall status
    local overall_status="${1:-1}" # Default to 1 (failure) if empty

    # Validate action
    if [[ "$ACTION" != "install" && "$ACTION" != "uninstall" ]]; then
        debug_print "Invalid action: $ACTION. Must be 'install' or 'uninstall'." "$debug"
        logE "Invalid action specified: $ACTION. Use 'install' or 'uninstall'."
        debug_end "$debug"
        return 1
    fi

    # Adjust log and output messages based on action
    local action_message
    if [[ "$ACTION" == "install" ]]; then
        action_message="Installation"
    else
        action_message="Uninstallation"
    fi

    # Dry runs describe plan evaluation and return before any normal install or
    # uninstall success message, URL, reboot reminder, or completion claim.
    if [[ "$DRY_RUN" == "true" ]]; then
        if [[ "$overall_status" -eq 0 ]]; then
            debug_print "$action_message dry run completed successfully; no changes were applied." "$debug"
            printf "\nDry run complete. No changes were applied.\n"
            debug_end "$debug"
            return 0
        fi
        logE "$action_message dry run failed while evaluating the $REPO_TITLE plan."
        debug_print "$action_message dry run encountered errors; no completion was claimed." "$debug"
        debug_end "$debug"
        return 1
    fi

    # Handle success or failure messages
    if [[ "$overall_status" -eq 0 ]]; then
        debug_print "$action_message completed successfully." "$debug"
        printf "\n%s successful: %s.\n" "$action_message" "$REPO_TITLE"
    else
        logE "$action_message failed: $REPO_TITLE."
        debug_print "$action_message encountered errors." "$debug"
        debug_end "$debug"
        return 1
    fi

    # Generate hostname and IP address
    local ip_address
    ip_address=$(hostname -I | awk '{print $1}')
    if [[ -z "$ip_address" ]]; then
        ip_address="unknown_ip"
    fi

    # Display follow-up instructions only if install was successful
    if [[ "$ACTION" == "install" && "$overall_status" -eq 0 && "${NO_WEB:-false}" != "true" ]]; then
        local repo_name="${REPO_NAME,,}"
        printf "\n"
        printf "To configure %s, open the following URL in your browser:\n\n" "$REPO_TITLE"

        printf "  %bhttp://%s.local/%s/%b\n" "${FGBLU}" "${HOSTNAME}" "${repo_name}" "${RESET}"
        printf "  %bhttp://%s/%s/%b\n\n" "${FGBLU}" "${ip_address}" "${repo_name}" "${RESET}"

        printf "If the hostname URL does not work, try using the IP address.\n"
        printf "Ensure your device is on the same network and that mDNS is\n"
        printf "supported by your system.\n\n"

        if [[ "${REBOOT:-false}" == "true" ]] && ! is_pi5; then
            printf "Remember to reboot to disable your soundcard before transmission.\n\n"
        fi
    elif [[ "$ACTION" == "install" && "$overall_status" -eq 0 ]]; then
        printf "\n%s installed with the web UI disabled (--no-web).\n\n" "$REPO_TITLE"
        if [[ "${REBOOT:-false}" == "true" ]] && ! is_pi5; then
            printf "Remember to reboot to disable your soundcard before transmission.\n\n"
        fi
    elif [[ "$ACTION" == "uninstall" && "$overall_status" -eq 0 ]]; then
        printf "\n%s has been uninstalled. Shared apt packages were retained.\n" "$REPO_TITLE"
        if [[ "$RP1_GPCLK_DKMS_PROVIDER_REMOVAL_UNPROVEN" == "true" ]]; then
            printf "The RP1-GPCLK-DKMS provider was not removed, as I was unable to prove\n"
            printf "WsprryPi's secure ownership of the module.\n"
        fi
        printf "\n"
        if [[ "${REBOOT:-false}" == "true" ]]; then
            printf "Remember to reboot to re-enable your soundcard.\n\n"
        fi
    fi

    debug_end "$debug"
    return "$overall_status"
}

# -----------------------------------------------------------------------------
# @brief Manages the installation or uninstallation of WsprryPi.
# @details This function dynamically executes a set of predefined functions to
#          either install or uninstall WsprryPi and its dependencies. The
#          functions are executed in order for installation and in reverse order
#          for uninstallation. Certain functions are skipped during uninstall.
#
# @global ACTION Specifies whether the function runs in 'install' or 'uninstall' mode.
# @global WSPR_SERVICE The executable name for WsprryPi.
# @global WSPR_INI The configuration file name for WsprryPi.
# @global WSPR_WATCH_EXE The WsprryPi watch executable.
# @global REPO_NAME The repository name for log file management.
#
# @param $1 Debug flag for enabling or disabling debug output.
#
# @throws Exits with status 1 if an invalid action is specified or any function
#         in the process fails.
#
# @return Returns 0 on success, or 1 if any operation fails.
#
# @example
#   manage_wsprry_pi "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317  # Ignore unreachable-code warnings in this function
manage_wsprry_pi() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    local service_command="/usr/local/bin/$WSPR_EXE -J -i /usr/local/etc/$WSPR_INI"
    if [[ "${NO_WEB:-false}" == "true" ]]; then
        service_command+=" --no-web"
    fi

    # Define the group of functions to install/uninstall
    local install_group=(
        "git_clone"
        "capture_install_sem_ver"
        "remove_legacy_services"
        "remove_legacy_files_and_dirs"
        "prepare_install_executable \"$WSPR_EXE\""
        "manage_exe \"$WSPR_EXE\""
        "manage_support_bundle_runtime"
        "manage_route_application"
        "manage_config \"$WSPR_INI\" \"/usr/local/etc/\""
        "manage_i2c"
        "manage_service \"/usr/bin/$WSPR_EXE\" \"$service_command\" \"false\""
        "activate_rp1_gpclk_runtime_administration"
        "manage_web"
        "manage_apache"
        "validate_wsprrypi_runtime"
        "manage_sound"
        "cleanup_files_in_directories"
        "restore_daemon_state"
    )

    # Define functions to skip on uninstall using an indexed array
    local skip_on_uninstall=(
        "git_clone"
        "capture_install_sem_ver"
        "prepare_install_executable"
        "cleanup_files_in_directories"
        "remove_legacy_services"
        "remove_legacy_files_and_dirs"
        "manage_sound"
        "restore_daemon_state"
    )

    # Track overall success/failure
    local overall_status=0

    # Debug print the original install_group list
    debug_print "Original install_group list:" "$debug"
    for func in "${install_group[@]}"; do
        debug_print "  - $func" "$debug"
    done

    # Select the execution order based on the action
    local group_to_execute=()
    case "$ACTION" in
    install)
        debug_print "(INSTALL) Creating group_to_execute list (after processing):" "$debug"
        group_to_execute=("${install_group[@]}")
        if [[ "${NO_WEB:-false}" == "true" ]]; then
            mapfile -t group_to_execute < <(
                printf '%s\n' "${group_to_execute[@]}" |
                    grep -v -E '^(manage_web|manage_apache)( |$)'
            )
        fi
        ;;
    uninstall)
        debug_print "(UNINSTALL) Reversing and filtering install_group…" "$debug"

        # Build a regex like: git_clone|remove_legacy_services|remove_legacy_files_and_dirs|cleanup_files_in_directories
        local skip_regex
        skip_regex=$(printf "|%s" "${skip_on_uninstall[@]}")
        skip_regex=${skip_regex:1}

        # Reverse and drop any line that starts with a skip-name
        # shellcheck disable=SC2015
        mapfile -t group_to_execute < <(
            printf '%s\n' "${install_group[@]}" |
                { command -v tac &>/dev/null && tac || awk '{lines[NR]=$0} END{for(i=NR;i>=1;i--)print lines[i]}'; } |
                grep -v -E "^($skip_regex)( |$)"
        )
        # The runtime deployment owns restoration of the application state it
        # captured. Remove it while that service and its companion still
        # exist, then continue with ordinary WsprryPi teardown.
        group_to_execute=(
            "prepare_owned_rp1_gpclk_runtime_removal"
            "remove_owned_rp1_gpclk_dkms_provider"
            "${group_to_execute[@]}"
        )
        # Re-add manage_sound at the very end
        group_to_execute+=("manage_sound")
        ;;
    *)
        die 1 "Invalid action: '$ACTION'. Use 'install' or 'uninstall'."
        ;;
    esac

    # Debug print the final group_to_execute list
    debug_print "Final group_to_execute list (after processing):" "$debug"
    for func in "${group_to_execute[@]}"; do
        debug_print "  - $func" "$debug"
    done

    # Execute functions
    for func in "${group_to_execute[@]}"; do
        local function_name="${func%% *}" # Extract only function name

        # Execute the function
        debug_print "Running $func() with action: '$ACTION'" "$debug"
        # Disable -e (crash out on error) temporarily
        set +e
        eval "$func \"$debug\""
        local status=$?
        set -e

        # Check if the function failed
        if [[ $status -ne 0 ]]; then
            logE "$func failed with status $status" "$debug"
            if [[ "$ACTION" == "install" ]]; then
                rollback_executable || return 1
                cleanup_files_in_directories "${func}" "${debug}"
                restore_daemon_state "${func}" "${debug}"
                overall_status=1
                break
            elif [[ "$ACTION" == "uninstall" ]]; then
                if [[ "$function_name" == "prepare_owned_rp1_gpclk_runtime_removal" ||
                    "$function_name" == "remove_owned_rp1_gpclk_dkms_provider" ]]; then
                    overall_status=1
                    break
                fi
                # Continue independent application teardown, but retain every
                # failure so uninstall success cannot be reported.
                overall_status=1
            fi
        else
            debug_print "$func succeeded." "$debug"
        fi
    done

    # Keep informational summaries after the per-step execution output. The
    # readiness check runs earlier in the group, but reaching this point with a
    # successful install proves that it and all subsequent steps completed.
    if [[ "$ACTION" == "install" && "$DRY_RUN" != "true" && "$overall_status" -eq 0 ]]; then
        logI "WsprryPi runtime readiness verified."
    fi

    # Indicate if a reboot will be necessary
    flag_need_reboot "$debug"

    # Finish the script, passing success or failure
    finish_script "$overall_status" "$debug"

    debug_end "$debug"
    return "$overall_status"
}

############
### Main Functions
############

# -----------------------------------------------------------------------------
# @brief Main execution function for the script.
# @details This function initializes the execution environment, processes
#          command-line arguments, verifies system compatibility, ensures
#          dependencies are installed, and executes either the install or
#          uninstall process for Wsprry Pi.
#
# @global ACTION Determines whether the script performs installation or
#                uninstallation.
#
# @param $@ Command-line arguments passed to the script.
#
# @throws Exits with an error code if any critical dependency is missing,
#         an unsupported environment is detected, or a step fails.
#
# @return 0 on successful execution, non-zero on failure.
#
# @note Debug mode (`"$debug"`) is propagated throughout all function calls.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
_main() {
    local debug
    local arg
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    [[ "$debug" != "debug" ]] && printf "\n" # Just a visual when not in debug.

    for arg in "$@"; do
        case "$arg" in
            -h|--help)
                usage
                debug_end "$debug"
                return 0
                ;;
            -v|--version)
                printf "%s: version %s\n" "$REPO_TITLE" "$SEM_VER"
                debug_end "$debug"
                return 0
                ;;
        esac
    done

    # Check and set up the environment
    handle_execution_context "$debug" # Get execution context and set environment variables
    process_args "$@" "$debug" || return $? # Parse command-line arguments
    validate_binary_options || return 1
    enforce_sudo "$debug"             # Ensure proper privileges for script execution
    setup_log "$debug"                # Setup logging environment

    logI "Checking environment." # Below takes a sec, show signs of life

    validate_depends "$debug"  # Ensure required dependencies are installed
    validate_sys_accs "$debug" # Verify critical system files are accessible
    validate_env_vars "$debug" # Check for required environment variables
    get_proj_params "$debug"   # Get project and git parameters
    [[ "$BINARY_SOURCE" == "build" ]] || BUILD_TYPE_OVERRIDE="RELEASE"
    resolve_build_settings "$debug"
    if [[ "${LOG_LEVEL_SET_FROM_ENV}" != "true" ]]; then
        if [[ "${WSPR_BUILD_TYPE}" == "RELEASE" ]]; then
            LOG_LEVEL="INFO"
        else
            LOG_LEVEL="DEBUG"
        fi
    fi
    logI "Resolved build type: ${WSPR_BUILD_TYPE} for branch ${REPO_BRANCH:-main}."

    check_bash "$debug"        # Ensure the script is executed in a Bash shell
    check_sh_ver "$debug"      # Verify the Bash version meets minimum requirements
    check_bitness "$debug"     # Validate system bitness compatibility
    check_release "$debug"     # Check Raspbian OS version compatibility
    check_arch "$debug"        # Validate Raspberry Pi model compatibility
    check_internet "$debug"    # Verify internet connectivity if required

    # Resolve the independently owned RP1 provider before any package-manager
    # mutation. Automatic selection skips non-Pi-5/unknown systems, while an
    # explicit true request is only an installation override.
    if [[ "$ACTION" != "uninstall" ]]; then
        prepare_rp1_gpclk_dkms_installation "$debug" || return 1
    fi

    # Start the script
    start_script "$debug"

    # The operator has confirmed the install; enforce build resources before
    # any package, service, or compilation mutation begins.
    if [[ "$ACTION" != "uninstall" ]]; then
        preflight_build_resources "$debug" || return 1
        prepare_precompiled_executable "$debug" || return 1
    fi

    # Print/display the environment
    print_model_name "$debug"   # Log board variant
    print_system "$debug"       # Log system information
    print_version "$debug"      # Log the script version

    # Feedback on multi-proc compilation
    [[ "$ACTION" != "uninstall" ]] && display_compilation_resource_notes "$debug"

    # Detect Apache web mode intent before package management or other
    # installation actions begin so the user sees the mode early.
    if [[ "$ACTION" != "uninstall" ]]; then
        detect_apache_web_mode "$debug"
    fi

    # Prepare an owned runtime, then install and validate dependencies.
    if [[ "$ACTION" != "uninstall" ]]; then
        logI "Updating and managing required packages (this may take a few minutes)."
        prepare_rp1_gpclk_runtime_update "$debug" || return 1
        handle_apt_packages "$debug" || return 1
        validate_support_bundle_age_dependency "$debug" || return 1
        validate_precompiled_runtime "$debug" || return 1
        validate_rp1_gpclk_build_dependencies "$debug" || return 1
        apply_rp1_gpclk_dkms_installation "$debug" || return 1
    fi

    # Handle correcting timezone
    [[ "$ACTION" != "uninstall" ]] && set_time "$debug"

    # After package management, Apache must actually be available unless web
    # installation has been explicitly disabled.
    if [[ "$ACTION" != "uninstall" ]]; then
        if ! validate_apache_web_availability "$debug"; then
            debug_end "$debug"
            return 1
        fi
    fi

    # Install or uninstall Wsprry Pi services
    manage_wsprry_pi "$debug" || return $?

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
# @brief Main function entry point.
# @details This function calls `_main` to initiate the script execution. By
#          calling `main`, we enable the correct reporting of the calling
#          function in Bash, ensuring that the stack trace and function call
#          are handled appropriately during the script execution.
#
# @param "$@" Arguments to be passed to `_main`.
# @return Returns the status code from `_main`.
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
main() {
    _main "$@"
    return "$?"
}

# -----------------------------------------------------------------------------
# @brief Traps the `EXIT` signal to invoke the `egress` function.
# @details Ensures the `egress` function is called automatically when the shell
#          exits. This enables proper session cleanup and displays session
#          statistics to the user.
# -----------------------------------------------------------------------------
if [[ -z "${BASH_SOURCE[0]:-}" || "${BASH_SOURCE[0]}" == "$0" ]]; then
    trap egress EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    trap 'exit 129' HUP

    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    main "$@" "$debug"
    retval="$?"
    debug_end "$debug"
    exit "$retval"
fi
