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
# REPO_BRANCH=dev ./install.sh --release
# curl -fsSL {url} | sudo bash
# curl -fsSL {url} | sudo bash -s -- debug
# curl -fsSL {url} | sudo env DRY_RUN=true bash -s -- debug
# curl -fsSL {url} | sudo env ACTION=uninstall bash -s -- debug
#
# -----------------------------------------------------------------------------

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
declare REPO_BRANCH="${REPO_BRANCH:-devel}"
declare GIT_TAG="${GIT_TAG:-v2.3.0}"
declare SEM_VER="${SEM_VER:-2.3.0}"
declare GIT_RAW_BASE="https://raw.githubusercontent.com"
declare GIT_API_BASE="https://api.github.com/repos"
declare GIT_CLONE_BASE="https://github.com"

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
#          `"false"`, non-main branches build as debug and use the `_debug`
#          executable suffix.
#
# @var BUILD_TYPE_OVERRIDE
# @brief Optional explicit build type override.
# @details When set to `"DEBUG"` or `"RELEASE"`, this overrides the branch-
#          based build selection logic.
#
# @var WSPR_BUILD_TYPE
# @brief The resolved build type for WsprryPi.
# @details This value is derived from `BUILD_TYPE_OVERRIDE`, `REPO_BRANCH`,
#          and `NON_MAIN_AS_RELEASE`.
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
# @details Applies `BUILD_TYPE_OVERRIDE` first. Otherwise, `main` and `master`
#          default to release builds. Non-main branches default to debug unless
#          `NON_MAIN_AS_RELEASE=true` is set.
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

    if [[ -z "${build_type}" ]]; then
        if [[ "${branch}" == "main" || "${branch}" == "master" ]]; then
            build_type="RELEASE"
        elif [[ "${NON_MAIN_AS_RELEASE}" == "true" ]]; then
            build_type="RELEASE"
        else
            build_type="DEBUG"
        fi
    fi

    WSPR_BUILD_TYPE="${build_type}"
    WSPR_EXE="${WSPR_SERVICE}$([[ "${WSPR_BUILD_TYPE}" == "DEBUG" ]] &&         printf '_debug')"

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
    USER_HOME=$(eval echo "~$SUDO_USER")
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
        # Unsupported models
        ["Raspberry Pi 5|5-model-b|bcm2712"]="Unsupported"
        # Supported models
        ["Raspberry Pi 400|400|bcm2711"]="Supported"
        ["Raspberry Pi Compute Module 4|4-compute-module|bcm2711"]="Supported"
        ["Raspberry Pi Compute Module 3|3-compute-module|bcm2837"]="Supported"
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
    "dpkg-reconfigure"
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
# @var APT_PACKAGES
# @type array
# @brief List of required APT packages.
# @details Defines the APT packages that the script depends on for its
#          execution. These packages should be available in the system's
#          default package repository. The script will check for their presence
#          and attempt to install any missing packages as needed.
#
#          Packages included:
#          - `jq`: JSON parsing utility.
#          - `git`: Version control system.
#
# @example
# for pkg in "${APT_PACKAGES[@]}"; do
#     if ! dpkg -l "$pkg" &>/dev/null; then
#         echo "Error: Required package '$pkg' is not installed."
#         exit 1
#     fi
# done
# -----------------------------------------------------------------------------
readonly APT_PACKAGES=(
    "git"
    "apache2"
    "php"
    "chrony"
    "libgpiod-dev"
    "libsystemd-dev"
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
    true
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
# @brief Executes a command in a separate Bash process.
# @details This function manages the execution of a shell command, handling the
#          display of status messages. It supports dry-run mode, where the
#          command is simulated without execution. The function prints success
#          or failure messages and handles the removal of the "Running" line
#          once the command finishes.
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
    # Start debug and filter parameters
    local debug status
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    # Declare local variables after debug initialization
    local status=0 exec_name running_pre complete_pre failed_pre
    local args cmd cmd_str

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

    # Join cmd[@] on spaces into one line
    cmd_str=$(printf '%s ' "${cmd[@]}")
    cmd_str=${cmd_str% }   # strip trailing space
    if [[ "$debug" == "debug" ]]; then
        logD "Name:    $exec_name"
        logD "Command: $cmd_str"
    fi

    # Prepare status prefixes
    running_pre="Running:" complete_pre="Complete:" failed_pre="Failed:"
    if [[ "$DRY_RUN" == "true" ]]; then
        running_pre+=" (dry)"
        complete_pre+=" (dry)"
        failed_pre+=" (dry)"
    fi

    # Show running status line
    printf "%b[  -  ]%b %s %s.\n" "${FGGLD}" "${RESET}" "$running_pre" "$exec_name"
    sleep 0.02
    if [[ "$DRY_RUN" == "true" ]]; then
        printf "%b%b" "$MOVE_UP" "$CLEAR_LINE"
        printf "%b[✔]%b %s %s.\n" "${FGGRN}" "${RESET}" "$complete_pre" "$exec_name"
        debug_end "$debug"
        return 0
    fi

    # Execute the command, swallowing output unless debug is enabled
    if [[ "$debug" == "debug" ]]; then
        "${cmd[@]}" || status=$?
    else
        "${cmd[@]}" &>/dev/null || status=$?
    fi

    # Clear running status line only when not in debug mode
    if [[ "$debug" != "debug" ]]; then
        printf "%b%b" "$MOVE_UP" "$CLEAR_LINE"
    fi

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
        sed -i "$sed_script" "$file" || {
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

    if [[ ! -f "$file_path" ]]; then
        debug_print "Error: File '$file_path' not found." "$debug"
        debug_end "$debug"
        return 1
    fi

    local pattern="%${search_string}%"

    # Use sed to replace all occurrences of %SEARCH_STRING% with REPLACE_STRING
    sed -i "s|${pattern}|${replace_string}|g" "$file_path"

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
        debug_end "$debug"
        die 1 "Failed to read or process /proc/device-tree/compatible."
    fi

    # Ensure a model was detected
    if [[ -z "${detected_model:-}" ]]; then
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

    debug_end "$debug"
    die 1 "Detected Raspberry Pi model '$detected_model' is not recognized."
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

    # Check the name of the calling function
    local caller="${FUNCNAME[1]}"

    if [[ "$caller" == "process_args" ]]; then
        printf "%s: version %s\n" "$REPO_TITLE" "$SEM_VER" # Display the script name and version
    else
        if [[ "$THIS_SCRIPT" == "piped_script" ]]; then
            logI "Running ${REPO_TITLE}'s install script, version $SEM_VER."
        else
            logI "Running ${REPO_TITLE}'s '${THIS_SCRIPT}', version $SEM_VER"
        fi
    fi

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

    if [[ "$REQUIRE_SUDO" == true ]]; then
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
        debug_end "$debug"
        die 1 "Failed to read or process /proc/device-tree/compatible. Ensure compatibility."
    fi

    # Check if the detected model is empty
    if [[ -z "${detected_model:-}" ]]; then
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
    if [[ -n "$1" && "$1" =~ ^(DEBUG|INFO|WARNING|ERROR|CRITICAL|EXTENDED)$ ]]; then
        severity="$1"
    fi

    # Process arguments
    if [[ -n "$2" ]]; then
        message="$2"
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
        runuser -u "$SUDO_USER" -- git clone -b "$REPO_BRANCH" --recurse-submodules -j8 \
            "$GIT_CLONE" "$dest_root" \
        "$debug" || {
        warn "Failed to clone repository from '$GIT_CLONE' to '$dest_root'"
        debug_end "$debug"
        return 1
    }

    debug_end "$debug"
    return "$retval"
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

    # Check if APT_PACKAGES is empty
    if [[ ${#APT_PACKAGES[@]} -eq 0 ]]; then
        logI "No packages specified in APT_PACKAGES. Skipping package handling."
        debug_print "APT_PACKAGES is empty, skipping execution." "$debug"
        debug_end "$debug"
        return 0
    fi

    local package
    local error_count=0
    local runtime_pkg=""
    local packages_to_install=()

    logI "Updating and managing required packages (this may take a few minutes)."

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

    # Resolve the correct runtime package for libgpiod-dev (libgpiod2/libgpiod3).
    runtime_pkg="$(resolve_libgpiod_runtime_pkg "$debug")"
    if [[ -z "$runtime_pkg" ]]; then
        warn "Failed to resolve required libgpiod runtime package."
        debug_print "Continuing without explicit libgpiod runtime package." \
            "$debug"
        ((error_count++))
    else
        debug_print "Using libgpiod runtime package '$runtime_pkg'." "$debug"
    fi

    # Build final install list: base packages + resolved libgpiod runtime package.
    packages_to_install=( "${APT_PACKAGES[@]}" )
    if [[ -n "$runtime_pkg" ]]; then
        packages_to_install+=( "$runtime_pkg" )
    fi

    # Install or upgrade each package in the list
    for package in "${packages_to_install[@]}"; do
        if dpkg-query -W -f='${Status}' "$package" 2>/dev/null \
            | grep -q "install ok installed"; then
            if ! exec_command \
                "Upgrade $package" \
                apt-get install --only-upgrade -y "${package}" "$debug"; then
                warn "Failed to upgrade package: $package."
                ((error_count++))
            fi
        else
            if ! exec_command \
                "Install $package" \
                apt-get install -y "${package}" "$debug"; then
                warn "Failed to install package: $package."
                ((error_count++))
            fi
        fi
    done

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
    "-v|--version 0 version Display $WSPR_SERVICE version 1"
    "--release 0 set_release_build Build as release regardless of branch 0"
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

                            # Remove the processed flag and its argument
                            args=("${args[@]:2}")
                            processed_argument=true
                        else
                            # Call the function with no arguments
                            $function_name
                            retval="$?"
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

    runuser -u "$SUDO_USER" -- mkdir -p "${staging_path}"
    if [[ ! -d "${staging_path:-}" ]]; then
        debug_end "$debug"
        die 1 "Executables source directory does not exist."
    fi

    type="${WSPR_BUILD_TYPE}"
    if [[ "${type}" != "DEBUG" && "${type}" != "RELEASE" ]]; then
        logE "Error: Invalid WSPR_BUILD_TYPE '${type}'."
        debug_end "$debug"
        return 1
    fi

    # Stop Daemon
    debug_print "Stopping daemon" "$debug"
    if systemctl is-active --quiet "$daemon_name" 2>/dev/null; then
        WAS_RUNNING="true"
        exec_command "Stop ${daemon_systemd_name}" \
            systemctl stop "${daemon_systemd_name}" "$debug" || {
                logE "Error: Unable to stop daemon."
                debug_end "$debug"
                return 1
            }
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
    exe_path="/usr/local/bin/${executable}"

    if [[ "$ACTION" == "install" ]]; then
        # Validate source file exists
        if [[ ! -f "$source_path" ]]; then
            logE "Error: Source file '$source_path' not found."
            debug_end "$debug"
            return 1
        fi

        # Install the application
        debug_print "Copying application." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: cp -f $source_path $exe_path"
        else
            exec_command "Install application" cp -f "${source_path}" "${exe_path}" "$debug" || {
                logE "Failed to install application."
                debug_end "$debug"
                return 1
            }
        fi

        # Change ownership on the application
        debug_print "Changing ownership on application." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: chown root:root $exe_path"
        else
            exec_command "Change ownership on application" chown root:root "${exe_path}" "$debug" || {
                logE "Failed to change ownership on application."
                debug_end "$debug"
                return 1
            }
        fi

        # Change permissions on the application to make it executable
        debug_print "Changing permissions on application." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: chmod 755 $exe_path"
        else
            exec_command "Make app executable" chmod 755 "${exe_path}" "$debug" || {
                logE "Failed to change permissions on application."
                debug_end "$debug"
                return 1
            }
        fi

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
                upgrade_ini "$old_path" "$source_path" "$merged_ini" "$debug"
                source_path="$merged_ini"
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
            "$(get_sem_ver "$debug")" \
            "$debug"

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
                "$(get_sem_ver "$debug")" \
                "$debug"

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
    local debug old_ini new_ini merged_ini rc backup_ini
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    old_ini="$1"
    new_ini="$2"
    merged_ini="$3"
    backup_ini="${old_ini}.pre_migration.bak"

    debug_print "Merging $old_ini → $merged_ini." "$debug"

    if [[ -f "${old_ini}" ]]; then
        exec_command             "Backup existing INI"             cp -f "${old_ini}" "${backup_ini}" "$debug" || {
            logE "INI backup failed."
            debug_end "$debug"
            return 1
        }
    fi

    # Run mawk and capture any stderr.
    rc=0
    if ! mawk '
    function trim(value) {
      gsub(/^[ 	]+|[ 	]+$/, "", value)
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
      sub(/^[^=]*=[ 	]*/, "", val)
      sub(/[ 	]*$/, "", val)

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
          gsub(/^[ 	]*/, "", tmp)
          leadWS = substr(orig, 1, length(orig) - length(tmp))

          tmp2 = tmp
          gsub(/[ 	]*$/, "", tmp2)
          trailerWS = substr(tmp, length(tmp2) + 1)

          printf("%s=%s%s%s%s
", left, leadWS, overrides[composite], trailerWS, comment)
          next
        }
      }

      print
    }
    ' "$old_ini" "$new_ini" >"$merged_ini" 2>/tmp/upgrade_ini.err; then
        rc=$?
        local err_details
        err_details=$(sed 's/^/  > /' /tmp/upgrade_ini.err)

        logE "INI merge failed (mawk exited $rc)." "$err_details"
        rm -f /tmp/upgrade_ini.err
        debug_end "$debug"
        return 1
    fi

    rm -f /tmp/upgrade_ini.err
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
    local syslog_identifier
    local retval=0 # Initialize return value

    daemon_name="${WSPR_SERVICE}" # Remove path
    source_path="$LOCAL_SYSTEMD_DIR/generic.service"
    daemon_systemd_name="${daemon_name}.service"
    # Install under /etc so it overrides anything in /lib
    service_path="/etc/systemd/system/${daemon_systemd_name}"
    syslog_identifier="$daemon_name"       # Use stripped daemon_exe

    if [[ "$ACTION" == "install" ]]; then
        if systemctl list-unit-files --type=service | grep -q "$daemon_systemd_name"; then
            if [[ "$DRY_RUN" != "true" ]]; then
                exec_command "Disable systemd service" systemctl disable "${daemon_systemd_name}" "$debug" || retval=1
                exec_command "Stop systemd service" systemctl stop "${daemon_systemd_name}" "$debug" || retval=1

                if systemctl is-enabled "$daemon_systemd_name" 2>/dev/null | grep -q "^masked$"; then
                    exec_command "Unmask systemd service" systemctl unmask "${daemon_systemd_name}" "$debug" || retval=1
                fi
            fi
        fi

        if [[ ! -f "$source_path" ]]; then
            warn "$source_path not found."
            retval=1
        elif [[ "$DRY_RUN" != "true" ]]; then
            # Remove any existing override in /etc
            if [[ -f "$service_path" ]]; then
                exec_command "Remove old unit in /etc" rm -f "${service_path}" "$debug" || retval=1
            fi
            # Now copy our fresh unit into /etc
            exec_command "Copy systemd file" cp -f "${source_path}" "${service_path}" "$debug" || retval=1
            debug_print "Updating $service_path." "$debug"

            replace_string_in_script "$service_path" "SEMANTIC_VERSION" "$(get_sem_ver "$debug")" "$debug"
            replace_string_in_script "$service_path" "DAEMON_NAME" "$daemon_name" "$debug"
            replace_string_in_script "$service_path" "EXEC_START" "$exec_start" "$debug"
            replace_string_in_script "$service_path" "SYSLOG_IDENTIFIER" "$syslog_identifier" "$debug"

            exec_command "Change ownership on systemd file" \
                chown root:root "$service_path" \
                "$debug" || retval=1

            exec_command "Change permissions on systemd file" \
                chmod 644 "$service_path" \
                "$debug" || retval=1

            exec_command "Enable systemd service" \
                systemctl enable "$daemon_systemd_name" \
                "$debug" || retval=1

            exec_command "Reload systemd" \
                systemctl daemon-reload \
                "$debug" || retval=1

            exec_command "Start systemd service" \
                systemctl restart "$daemon_systemd_name" \
                "$debug" || retval=1
        fi

    elif [[ "$ACTION" == "uninstall" ]]; then
        if [[ -f "$service_path" ]]; then
            if systemctl list-unit-files | grep -q "^$daemon_systemd_name"; then
                if systemctl is-active --quiet "$daemon_systemd_name"; then
                    exec_command "Stop systemd service" systemctl stop "${daemon_systemd_name}" "$debug" || retval=1
                fi

                if systemctl is-enabled --quiet "$daemon_systemd_name"; then
                    exec_command "Disable systemd service" systemctl disable "${daemon_systemd_name}" "$debug" || retval=1
                fi
            fi

            exec_command "Remove service file" rm -f "${service_path}" "$debug" || retval=1
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

    if [[ "$ACTION" == "install" ]]; then
        # Validate source directory exists
        if [[ ! -d "$source_path" ]]; then
            logE "Error: Source directory '$source_path' not found."
            debug_end "$debug"
            return 1
        fi

        # Ensure target directory exists
        debug_print "Creating target web directory if missing." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: mkdir -p $target_path"
        else
            exec_command "Create target web directory" mkdir -p "${target_path}" "$debug" || retval=1
        fi

        # Copy web files
        debug_print "Copying web files from '$source_path' to '$target_path'." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: cp -r $source_path* $target_path/"
        else
            exec_command "Copy web files" \
                cp -r "${source_path}"/* "${target_path}/" \
                "$debug" || retval=1
        fi

        # Change ownership and permissions
        debug_print "Setting permissions for '$target_path'." "$debug"
        if [[ "$DRY_RUN" == "true" ]]; then
            logD "Exec: sudo chown -R www-data:www-data $target_path"
            logD "Exec: sudo chmod -R 755 $target_path"
            logD "Exec: sudo usermod -aG systemd-journal www-data"
        else
            # Set ownership for the entire directory
            exec_command "Set ownership" chown -R www-data:www-data "${target_path}" "$debug" || retval=1

            # Set correct permissions:
            # - Directories: `rwxr-xr-x` (755)
            # - Files: `rw-r--r--` (644)
            exec_command "Set directory permissions" \
                find "$target_path" -type d -exec sudo chmod 755 {} + \
                "$debug" || retval=1

            exec_command "Set file permissions" \
                find "$target_path" -type f -exec sudo chmod 644 {} + \
                "$debug" || retval=1

            exec_command "Set log permissions" \
                sudo usermod -aG systemd-journal www-data \
                "$debug" || retval=1
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
# @brief Manages the sound module for WsprryPi.
# @details This function enables or disables the Raspberry Pi sound module
#          (`snd_bcm2835`) by modifying the ALSA blacklist file. During
#          installation, the module is blacklisted to allow WsprryPi to
#          generate radio frequencies. During uninstallation, the blacklist
#          entry is removed to restore sound functionality.
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

    if [[ "$ACTION" == "install" ]]; then
        if [[ ! -f "$file" ]]; then
            # Create file and add the blacklist line
            echo "$blacklist" >"$file"
            REBOOT="true"
            debug_print "Created $file and disabled sound." "$debug"
        elif ! grep -Fxq "$blacklist" "$file"; then
            # Append blacklist line if it doesn't exist
            echo "$blacklist" >>"$file"
            REBOOT="true"
            debug_print "Added blacklist entry to $file." "$debug"
        else
            REBOOT="false"
            debug_print "Sound is already disabled." "$debug"
        fi

    elif [[ "$ACTION" == "uninstall" ]]; then
        if [[ -f "$file" ]] && grep -Fxq "$blacklist" "$file"; then
            line_count=$(wc -l <"$file")

            if [[ "$line_count" -eq 1 ]]; then
                # If it's the only line, delete the file
                rm -f "$file"
                debug_print "Removed $file and re-enabled sound." "$debug"
            else
                # Otherwise, remove just the blacklist line
                sed -i "\|$blacklist|d" "$file"
                debug_print "Removed blacklist entry from $file." "$debug"
            fi
            REBOOT="true"
        else
            REBOOT="false"
            debug_print "Sound is already enabled or no blacklist file exists." "$debug"
        fi
    else
        die 1 "Invalid action. Use 'install' or 'uninstall'."
    fi

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
#         • REST proxies (31415) for /wsprrypi/config & /version
#         • WS proxy (31416) for /wsprrypi/socket
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
    local config_file source_path site_conf server_name

    config_file="wsprrypi.conf"
    source_path="${LOCAL_CONFIG_DIR}/${config_file}"
    site_conf="${DEFAULT_SITES_CONF}/${config_file}"
    server_name="${DEFAULT_SERVERNAME}"

    if [[ -z "$ACTION" || "$ACTION" == install ]]; then
        # Ensure ServerName is present in the main Apache config
        if ! grep -qF "$server_name" "$APACHE_CONF"; then
            exec_command "Insert ServerName directive" \
                sed -i "1i $server_name" "$APACHE_CONF" \
                "$debug"
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

        # If stock page, comment out log lines in the vhost before enabling
        if is_stock_apache_page "${site_conf}" "$debug"; then
            modify_comment_lines "${site_conf}" ".log" comment "$debug"
        fi

        # Enable required modules
        exec_command "Enable proxy modules" a2enmod proxy proxy_http proxy_wstunnel "$debug" || {
            logE "Failed to enable proxy modules."
            debug_end "$debug"
            return 1
        }

        # Disable default site
        exec_command "Disable default site" a2dissite 000-default.conf "$debug" || {
            logE "Failed to disable default site."
            debug_end "$debug"
            return 1
        }

        # Enable wsprrypi site
        exec_command "Enable wsprrypi site" a2ensite wsprrypi.conf "$debug" || {
            logE "Failed to enablw wsprrypi site."
            debug_end "$debug"
            return 1
        }

    else
        # Uninstall path
        exec_command "Disable wsprrypi site" a2dissite wsprrypi.conf "$debug"

        exec_command "Enable default site" a2ensite 000-default.conf "$debug"

        exec_command "Remove wsprrypi available config" rm -f "${site_conf}" "$debug"

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

    # Must exist and be readable
    [[ -r "$file" ]] || return 1

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

# -----------------------------------------------------------------------------
# @brief Display a reboot prompt when required.
# @details
#   Checks the global REBOOT flag. If "true", prints a message
#   explaining why a reboot is needed (install or uninstall path),
#   shows the reboot command, and waits for a key press.
#
# @global ACTION  Install or uninstall mode indicator.
# @global REBOOT  Semaphore flag for reboot requirement.
#
# @param $1  Optional debug flag to enable debug output.
#
# @return Always returns 0.
#
# @example
#   flag_need_reboot "debug"
# -----------------------------------------------------------------------------
# shellcheck disable=SC2317
# shellcheck disable=SC2329
flag_need_reboot() {
    local debug
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"

    if [[ "$REBOOT" == "true" ]]; then
        printf "\n*Important Note:*\n\n"
        if [[ "$ACTION" == "install" ]]; then
            printf "Wsprry Pi uses the same hardware as the sound system to\n"
            printf "generate radio frequencies. This soundcard has been disabled.\n"
            printf "Reboot with 'sudo reboot' after install for changes to take\n"
            printf "effect.\n\n"
        else
            printf "The sound system has been re-enabled. To use audio, reboot\n"
            printf "with 'sudo reboot' for changes to take effect.\n\n"
        fi
        read -rp "Press any key to continue." </dev/tty
        echo
    fi

    debug_end "$debug"
    return 0
}

# shellcheck disable=SC2317
restore_daemon_state() {
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

        read -rp "Press any key to continue." </dev/tty
        echo
    fi

    debug_end "$debug"
    return 0
}

# -----------------------------------------------------------------------------
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
    if [[ "$ACTION" == "install" && "$overall_status" -eq 0 ]]; then
        local repo_name="${REPO_NAME,,}"
        printf "\n"
        printf "To configure %s, open the following URL in your browser:\n\n" "$REPO_TITLE"

        printf "  %bhttp://%s.local/%s/%b\n" "${FGBLU}" "${HOSTNAME}" "${repo_name}" "${RESET}"
        printf "  %bhttp://%s/%s/%b\n\n" "${FGBLU}" "${ip_address}" "${repo_name}" "${RESET}"

        printf "If the hostname URL does not work, try using the IP address.\n"
        printf "Ensure your device is on the same network and that mDNS is\n"
        printf "supported by your system.\n\n"

        if [[ "${REBOOT:-false}" == "true" ]]; then
            printf "Remember to reboot to disable your soundcard before transmission.\n\n"
        fi
    elif [[ "$ACTION" == "uninstall" && "$overall_status" -eq 0 ]]; then
        printf "\n%s has been uninstalled. No apt packages have\n" "$REPO_TITLE"
        printf "been removed to prevent impact to other functionality.\n\n"
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

    # Define the group of functions to install/uninstall
    local install_group=(
        "git_clone"
        "remove_legacy_services"
        "remove_legacy_files_and_dirs"
        "compile_binary \"$WSPR_EXE\""
        "manage_exe \"$WSPR_EXE\""
        "manage_config \"$WSPR_INI\" \"/usr/local/etc/\""
        "manage_service \"/usr/bin/$WSPR_EXE\" \"/usr/local/bin/$WSPR_EXE -J -i /usr/local/etc/$WSPR_INI\" \"false\""
        "manage_web"
        "manage_apache"
        "manage_sound"
        "cleanup_files_in_directories"
        "restore_daemon_state"
    )

    # Define functions to skip on uninstall using an indexed array
    local skip_on_uninstall=(
        "git_clone"
        "compile_binary"
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
                cleanup_files_in_directories "${func}" "${debug}"
                restore_daemon_state "${func}" "${debug}"
                overall_status=1
                break
            fi
        else
            debug_print "$func succeeded." "$debug"
        fi
    done

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
    debug=$(debug_start "$@")
    eval set -- "$(debug_filter "$@")"
    [[ "$debug" != "debug" ]] && printf "\n" # Just a visual when not in debug.

    # Check and set up the environment
    handle_execution_context "$debug" # Get execution context and set environment variables
    process_args "$@" "$debug"        # Parse command-line arguments
    enforce_sudo "$debug"             # Ensure proper privileges for script execution
    setup_log "$debug"                # Setup logging environment

    logI "Checking environment." # Below takes a sec, show signs of life

    validate_depends "$debug"  # Ensure required dependencies are installed
    validate_sys_accs "$debug" # Verify critical system files are accessible
    validate_env_vars "$debug" # Check for required environment variables
    get_proj_params "$debug"   # Get project and git parameters
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

    # Start the script
    start_script "$debug"

    # Print/display the environment
    print_model_name "$debug"   # Log board variant
    print_system "$debug"       # Log system information
    print_version "$debug"      # Log the script version

    # Feedback on multi-proc compilation
    [[ "$ACTION" != "uninstall" ]] && display_compilation_resource_notes "$debug"

    # Install dependencies after system checks
    [[ "$ACTION" != "uninstall" ]] && handle_apt_packages "$debug"

    # Handle correcting timezone
    [[ "$ACTION" != "uninstall" ]] && set_time "$debug"

    # Install or uninstall Wsprry Pi services
    manage_wsprry_pi "$debug"

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
trap egress EXIT

debug=$(debug_start "$@")
eval set -- "$(debug_filter "$@")"
main "$@" "$debug"
retval="$?"
debug_end "$debug"
exit "$retval"
