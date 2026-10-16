#!/usr/bin/env bash
set -u
set -o pipefail
umask 077

PROJECT_NAME="WsprryPi"
SERVICE_NAMES=("wsprrypi" "apache2")
SYSLOG_IDENTIFIERS=("wsprrypi" "apache2")
INSTALLED_EXE="/usr/local/bin/wsprrypi"
INSTALLED_DEBUG_EXE="/usr/local/bin/wsprrypi_debug"
INSTALLED_INI="/usr/local/etc/wsprrypi.ini"
INSTALLED_SERVICE="/etc/systemd/system/wsprrypi.service"
LEGACY_LOG_DIR="/var/log/wsprrypi"
INSTALL_LOG_NAME="wsprrypi.log"
DEFAULT_WEB_PORT="31415"
DEFAULT_SOCKET_PORT="31416"
RP1_DKMS="/usr/sbin/dkms"
RP1_MODINFO="/usr/sbin/modinfo"
RP1_PYTHON="/usr/bin/python3"
RP1_TIMEOUT="/usr/bin/timeout"
RP1_RUNTIME_PROVIDER="/usr/lib/rp1-gpclk-dkms/runtime_provider.py"
RP1_QUALIFICATION="/usr/local/lib/wsprrypi/rp1_qualification.py"
RP1_INSTALLATION_RECORD="/var/lib/wsprrypi/rp1-gpclk-dkms-installation.json"

STAMP="$(date -u +"%Y%m%dT%H%M%SZ")"
HOST="$(hostname -s 2>/dev/null || echo raspberrypi)"
HOST="${HOST//[^A-Za-z0-9._-]/_}"
OUT_ROOT=""
OUT_DIR=""
ARCHIVE=""
ARCHIVE_NAME=""
SHA256_FILE=""
RESULT_FILE=""
ARCHIVE_TMP=""
SHA256_TMP=""
OUTPUT_DIR=""
OUTPUT_DIR_SUPPLIED=0

INCLUDE_CONFIGS=1
INCLUDE_FULL_LOGS=0
KEEP_WORKDIR=0
PROJECT_PATH=""
PROBE_I2C=0
I2C_PROBE_STATUS="not_requested"
PRIVILEGED_DIAGNOSTICS_INCOMPLETE=0
CASE_ID=""
CONTEXT_KIND=""
GITHUB_ISSUE_URL=""
PROBLEM_DESCRIPTION_FILE=""
CONTACT_FILE=""
PROBLEM_DESCRIPTION=""
CONTACT_VALUE=""
PROJECT_VERSION=""
MANIFEST_INCLUDED=0
SYMLINKS_OMITTED=0

usage() {
  cat <<EOF
${PROJECT_NAME} support bundle collector

Creates a local .tar.gz support bundle for WsprryPi troubleshooting.

Usage:
  curl -fsSL <url>/collect-support-bundle.sh | bash
  curl -fsSL <url>/collect-support-bundle.sh | bash -s -- [options]

Options:
  --path PATH          WsprryPi checkout/install path
  --output-dir DIR     Write archive, SHA-256, and JSON result to existing private DIR
  --no-configs         Do not include redacted config files
  --full-logs          Include larger journal/log output
  --probe-i2c          Run the fixed active I2C scan: i2cdetect -y 1
  --case-id ID         Private-intake case ID (requires one support context path)
  --github-issue URL   Existing WsprryPi issue for private intake
  --context-kind KIND  new_github_issue or no_github
  --problem-description-file FILE  Private bounded description file
  --contact-file FILE  Private bounded contact file
  --project-version VERSION  Application-supplied semantic version for private intake
  --keep-workdir       Keep temporary collection directory
  -h, --help           Show this help

Notes:
  - No data is uploaded by this script.
  - Passwords, tokens, upload secrets, and common credential fields are redacted.
  - This script is intended for Raspberry Pi / Linux WsprryPi systems.
  - Without --output-dir, the archive is created in the current directory (legacy behavior).
  - The collector writes <archive>.result.json alongside every completed bundle; with --output-dir,
    it chooses the archive name in that directory without relying on the caller's working directory.
    The JSON result records completion status, artifact names/digest, UTC timestamp, selected
    collection options, I2C probe status, and whether privileged diagnostics may be incomplete.
  - Active I2C scanning is never automatic. --probe-i2c only permits i2cdetect -y 1.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --path)
      shift
      PROJECT_PATH="${1:-}"
      ;;
    --output-dir)
      shift
      OUTPUT_DIR="${1:-}"
      OUTPUT_DIR_SUPPLIED=1
      ;;
    --no-configs) INCLUDE_CONFIGS=0 ;;
    --full-logs) INCLUDE_FULL_LOGS=1 ;;
    --probe-i2c) PROBE_I2C=1 ;;
    --case-id)
      shift
      CASE_ID="${1:-}"
      ;;
    --github-issue)
      shift
      GITHUB_ISSUE_URL="${1:-}"
      ;;
    --context-kind)
      shift
      CONTEXT_KIND="${1:-}"
      ;;
    --problem-description-file)
      shift
      PROBLEM_DESCRIPTION_FILE="${1:-}"
      ;;
    --contact-file)
      shift
      CONTACT_FILE="${1:-}"
      ;;
    --project-version)
      shift
      PROJECT_VERSION="${1:-}"
      ;;
    --keep-workdir) KEEP_WORKDIR=1 ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
  shift
done

json_bool() {
  [[ "$1" -eq 1 ]] && printf 'true' || printf 'false'
}

json_string_or_null() {
  if [[ -n "$1" ]]; then
    printf '"%s"' "$1"
  else
    printf 'null'
  fi
}

json_escape() {
  local value="$1"
  value="${value//\\/\\\\}"
  value="${value//\"/\\\"}"
  printf '%s' "$value"
}

valid_case_id() {
  [[ "$1" =~ ^[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4}$ ]]
}

valid_project_version() {
  local value="$1" core prerelease identifier
  [[ ${#value} -le 128 && "$value" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ]] || return 1
  core="${value%%+*}"
  [[ "$core" == *-* ]] || return 0
  prerelease="${core#*-}"
  while [[ -n "$prerelease" ]]; do
    identifier="${prerelease%%.*}"
    [[ ! "$identifier" =~ ^[0-9]+$ || "$identifier" == "0" || "$identifier" != 0* ]] || return 1
    [[ "$prerelease" == *.* ]] || break
    prerelease="${prerelease#*.}"
  done
}

read_private_context_file() {
  local path="$1" maximum="$2" label="$3" size value mode owner
  [[ "$path" == /* && ! -L "$path" && -f "$path" ]] || fail "$label must be an absolute regular non-symlink file."
  owner="$(stat -c '%u' "$path" 2>/dev/null || true)"
  mode="$(stat -c '%a' "$path" 2>/dev/null || true)"
  size="$(stat -c '%s' "$path" 2>/dev/null || true)"
  [[ "$owner" == "$(id -u)" && "$mode" =~ ^[0-7]{3,4}$ && "$size" =~ ^[0-9]+$ ]] || fail "$label metadata is unavailable."
  (( (8#${mode: -2:1} & 2) == 0 && (8#${mode: -1} & 2) == 0 )) || fail "$label must not be group- or world-writable."
  (( size > 0 && size <= maximum )) || fail "$label is empty or too large."
  if LC_ALL=C grep -q '[[:cntrl:]]' "$path" 2>/dev/null; then fail "$label contains unsupported control characters."; fi
  perl -MEncode -e 'use Encode qw(decode FB_CROAK); local $/; my $value = <>; eval { decode("UTF-8", $value, FB_CROAK) }; exit($@ ? 1 : 0)' "$path" || fail "$label is not valid UTF-8."
  value="$(cat "$path")" || fail "Unable to read $label."
  [[ "$value" != *$'\n'* && "$value" != *$'\r'* && "$value" != *$'\t'* ]] || fail "$label must contain one line of text."
  [[ -n "${value//[[:space:]]/}" ]] || fail "$label must contain meaningful text."
  printf '%s' "$value"
}

validate_private_metadata() {
  local any_private=0
  [[ -n "$CASE_ID$CONTEXT_KIND$GITHUB_ISSUE_URL$PROBLEM_DESCRIPTION_FILE$CONTACT_FILE$PROJECT_VERSION" ]] && any_private=1
  [[ "$any_private" -eq 1 ]] || return 0
  valid_case_id "$CASE_ID" || fail "Private intake requires a valid case ID."
  valid_project_version "$PROJECT_VERSION" || fail "Private intake requires a valid project version."
  if [[ -n "$GITHUB_ISSUE_URL" ]]; then
    [[ -z "$CONTEXT_KIND$PROBLEM_DESCRIPTION_FILE$CONTACT_FILE" ]] || fail "Private intake support context is conflicting."
    [[ "$GITHUB_ISSUE_URL" =~ ^https://github\.com/WsprryPi/WsprryPi/issues/[1-9][0-9]{0,9}$ ]] || fail "GitHub issue URL is invalid."
    CONTEXT_KIND="existing_github_issue"
  else
    [[ "$CONTEXT_KIND" == "new_github_issue" || "$CONTEXT_KIND" == "no_github" ]] || fail "Private intake context kind is invalid."
    [[ -n "$PROBLEM_DESCRIPTION_FILE" && -n "$CONTACT_FILE" ]] || fail "Private intake description and contact files are required."
    PROBLEM_DESCRIPTION="$(read_private_context_file "$PROBLEM_DESCRIPTION_FILE" 4096 "Problem description file")" || exit 1
    CONTACT_VALUE="$(read_private_context_file "$CONTACT_FILE" 512 "Contact file")" || exit 1
  fi
}

publish_result() {
  local status="$1"
  local archive_name="$2"
  local sha_name="$3"
  local digest="$4"
  local temporary_result

  [[ -n "$RESULT_FILE" ]] || return 0
  temporary_result="${RESULT_FILE}.tmp.$$"
  {
    printf '{\n'
    printf '  "schema_version": 1,\n'
    printf '  "status": "%s",\n' "$status"
    printf '  "archive_filename": '; json_string_or_null "$archive_name"; printf ',\n'
    printf '  "sha256_filename": '; json_string_or_null "$sha_name"; printf ',\n'
    printf '  "sha256": '; json_string_or_null "$digest"; printf ',\n'
    printf '  "generated_at_utc": "%s",\n' "$STAMP"
    printf '  "configuration_files_included": '; json_bool "$INCLUDE_CONFIGS"; printf ',\n'
    printf '  "full_logs_included": '; json_bool "$INCLUDE_FULL_LOGS"; printf ',\n'
    printf '  "i2c_probe_requested": '; json_bool "$PROBE_I2C"; printf ',\n'
    printf '  "i2c_probe_status": "%s",\n' "$I2C_PROBE_STATUS"
    printf '  "privileged_diagnostics_may_be_incomplete": '; json_bool "$PRIVILEGED_DIAGNOSTICS_INCOMPLETE"; printf ',\n'
    printf '  "case_id": '; json_string_or_null "$CASE_ID"; printf ',\n'
    printf '  "manifest_included": '; json_bool "$MANIFEST_INCLUDED"; printf '\n'
    printf '}\n'
  } > "$temporary_result" || return 1
  chmod 600 "$temporary_result" || return 1
  ln "$temporary_result" "$RESULT_FILE" || { rm -f "$temporary_result"; return 1; }
  rm -f "$temporary_result"
}

fail() {
  local message="$1"
  echo "$message" >&2
  publish_result failure "" "" "" || true
  exit 1
}

validate_output_dir() {
  local mode
  [[ "$OUTPUT_DIR" == /* ]] || fail "Output directory must be an absolute path."
  [[ ! -L "$OUTPUT_DIR" && -d "$OUTPUT_DIR" ]] || fail "Output directory must be an existing non-symlink directory."
  [[ -w "$OUTPUT_DIR" && -x "$OUTPUT_DIR" ]] || fail "Output directory is not writable and searchable by this user."
  mode="$(stat -c '%a' "$OUTPUT_DIR" 2>/dev/null || true)"
  [[ "$mode" =~ ^[0-7]{3,4}$ ]] || fail "Unable to determine output directory permissions."
  (( (8#${mode: -2:1} & 2) == 0 && (8#${mode: -1} & 2) == 0 )) || fail "Output directory must not be group- or world-writable."
  OUTPUT_DIR="$(cd -P "$OUTPUT_DIR" && pwd -P)" || fail "Unable to resolve output directory."
}

if [[ "$OUTPUT_DIR_SUPPLIED" -eq 1 ]]; then
  validate_output_dir
else
  OUTPUT_DIR="$PWD"
fi

ARCHIVE_NAME="${PROJECT_NAME}-support-${HOST}-${STAMP}.tar.gz"
ARCHIVE="${OUTPUT_DIR}/${ARCHIVE_NAME}"
SHA256_FILE="${ARCHIVE}.sha256"
RESULT_FILE="${ARCHIVE}.result.json"

validate_private_metadata

if [[ -e "$ARCHIVE" || -e "$SHA256_FILE" || -e "$RESULT_FILE" ]]; then
  fail "Refusing to overwrite an existing support-bundle artifact."
fi

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/${PROJECT_NAME}-support-${STAMP}.XXXXXX")" || fail "Unable to create private temporary work directory."
chmod 700 "$OUT_ROOT" || fail "Unable to secure temporary work directory."
OUT_DIR="${OUT_ROOT}/bundle"
mkdir -p "$OUT_DIR"/{system,project,logs,configs/{installed,project,systemd},hardware,web,network,commands,processes} || fail "Unable to create support-bundle work directory."

cleanup() {
  [[ -n "$ARCHIVE_TMP" ]] && rm -f "$ARCHIVE_TMP"
  [[ -n "$SHA256_TMP" ]] && rm -f "$SHA256_TMP"
  if [[ "$KEEP_WORKDIR" -eq 0 ]]; then
    rm -rf "$OUT_ROOT"
  else
    echo "Temporary work directory kept at: $OUT_ROOT"
  fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP

log() {
  printf '[%s] %s\n' "$(date +"%H:%M:%S")" "$*"
}

run_cmd() {
  local name="$1"
  shift
  local outfile="${OUT_DIR}/commands/${name}.txt"

  {
    echo "\$ $*"
    echo
    "$@"
  } >"$outfile" 2>&1 || {
    local status=$?
    {
      echo
      echo "[command exited with status: $status]"
    } >>"$outfile"
  }
}

run_optional_absolute_cmd() {
  local name="$1" executable="$2"
  shift 2
  if [[ -x "$executable" ]]; then
    run_cmd "$name" "$executable" "$@"
  else
    {
      echo "\$ $executable $*"
      echo
      echo "Collection status: command unavailable ($executable)"
    } > "${OUT_DIR}/commands/${name}.txt"
  fi
}

run_bounded_absolute_cmd() {
  local name="$1"
  shift
  if [[ -x "$RP1_TIMEOUT" ]]; then
    run_cmd "$name" "$RP1_TIMEOUT" --signal=TERM --kill-after=5s 30s "$@"
  else
    {
      echo "\$ $*"
      echo
      echo "Collection status: bounded command runner unavailable ($RP1_TIMEOUT)"
    } > "${OUT_DIR}/commands/${name}.txt"
  fi
}

collect_rp1_record_summary() {
  run_bounded_absolute_cmd rp1_gpclk_ownership_identity "$RP1_PYTHON" -I - "$RP1_INSTALLATION_RECORD" <<'PY_RP1_IDENTITY'
import json, os, re, stat, sys
OWNER_UID = 0

def pairs(items):
    value = {}
    for key, item in items:
        if key in value: raise ValueError('duplicate field')
        value[key] = item
    return value

def summarize(value):
    if (not isinstance(value, dict) or value.get('owner') != 'WsprryPi' or
        value.get('repository') != 'WsprryPi/RP1-GPCLK-DKMS' or
        value.get('schema') not in ('wsprrypi-rp1-gpclk-dkms-ownership-v2',
                                   'wsprrypi-rp1-gpclk-dkms-ownership-v3') or
        value.get('channel') not in ('development', 'release') or
        value.get('qualificationClaim') is not False):
        raise ValueError('untrusted record')
    manifest = value.get('manifest', {})
    if not isinstance(manifest, dict): raise ValueError('invalid manifest')
    result = {'status': 'recorded-only', 'liveValidated': False,
              'selection': value['channel'], 'qualificationEstablished': False,
              'publicationVerified': False}
    fields = {'sourceCommit': (value.get('sourceCommit'), '[0-9a-f]{40}'),
              'sourceTree': (value.get('sourceTree'), '[0-9a-f]{40}'),
              'productVersion': (value.get('productVersion', manifest.get('productVersion')), r'[0-9]+\.[0-9]+\.[0-9]+'),
              'debianVersion': (manifest.get('debianVersion'), r'[0-9]+\.[0-9]+\.[0-9]+-[0-9]+'),
              'recordedReleaseTag': (value.get('tag'), r'v[0-9]+\.[0-9]+\.[0-9]+'),
              'consumerUapiSha256': (value.get('uapiSha256', manifest.get('uapi', {}).get('sha256'))
                   if isinstance(manifest.get('uapi', {}), dict) else None, '[0-9a-f]{64}')}
    for key, (item, pattern) in fields.items():
        if item is not None:
            if not isinstance(item, str) or re.fullmatch(pattern, item) is None:
                raise ValueError('invalid identity')
            result[key] = item
    runtime = value.get('runtime', {})
    if not isinstance(runtime, dict): raise ValueError('invalid runtime')
    for key in ('bindingSha256', 'artifactSetSha256'):
        item = runtime.get(key)
        if item is not None:
            if not isinstance(item, str) or re.fullmatch('[0-9a-f]{64}', item) is None:
                raise ValueError('invalid runtime identity')
            result[key] = item
    identities = runtime.get('compatibilityIdentities', {})
    if not isinstance(identities, dict): raise ValueError('invalid route identities')
    result['compatibilityIdentities'] = {}
    for route in ('gpio4', 'gpio20'):
        if route in identities:
            item = identities[route]
            if not isinstance(item, str) or re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+-rp1-'+route, item) is None:
                raise ValueError('invalid route identity')
            result['compatibilityIdentities'][route] = item
    return result

def read_summary(path):
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        try:
            info = os.fstat(fd)
            if (not stat.S_ISREG(info.st_mode) or info.st_uid != OWNER_UID or
                info.st_mode & 0o077 or info.st_size > 4*1024*1024):
                raise ValueError('untrusted record file')
            with os.fdopen(fd, 'rb', closefd=False) as stream: data = stream.read(4*1024*1024+1)
            if len(data) > 4*1024*1024: raise ValueError('read bound')
        finally: os.close(fd)
        return summarize(json.loads(data, object_pairs_hook=pairs))
    except FileNotFoundError: return {'status': 'absent'}
    except (OSError, ValueError, TypeError, KeyError): return {'status': 'unavailable-or-invalid'}

if __name__ == '__main__':
    print(json.dumps({'contract': 'wsprrypi-rp1-ownership-diagnostics-v1',
        'installation': read_summary(sys.argv[1]),
        'pendingRemoval': read_summary(sys.argv[1]+'.removal-pending')}))
PY_RP1_IDENTITY
}

safe_root_owned_regular_file() {
  local path="$1" owner mode
  [[ -f "$path" && ! -L "$path" ]] || return 1
  owner="$(stat -c '%u' "$path" 2>/dev/null || true)"
  mode="$(stat -c '%a' "$path" 2>/dev/null || true)"
  [[ "$owner" == "0" && "$mode" =~ ^[0-7]{3,4}$ ]] || return 1
  (( (8#${mode: -2:1} & 2) == 0 && (8#${mode: -1} & 2) == 0 ))
}

copy_if_exists() {
  local src="$1"
  local dest_dir="$2"

  if [[ -e "$src" ]]; then
    mkdir -p "$dest_dir"
    cp -R "$src" "$dest_dir/" 2>/dev/null || true
  fi
}

tail_or_copy_log() {
  local src="$1"
  local dest="$2"

  [[ -f "$src" ]] || return 0
  mkdir -p "$(dirname "$dest")"

  if [[ "$INCLUDE_FULL_LOGS" -eq 1 ]]; then
    cp "$src" "$dest" 2>/dev/null || true
  else
    tail -n 1000 "$src" > "$dest" 2>/dev/null || true
  fi
}

collect_install_logs() {
  local dest_dir="${OUT_DIR}/logs/install"
  local found=0
  local candidate
  local sudo_home=""

  mkdir -p "$dest_dir"

  if [[ -n "${SUDO_USER:-}" ]]; then
    sudo_home="$(getent passwd "$SUDO_USER" 2>/dev/null | cut -d: -f6 || true)"
  fi

  local candidates=()

  if [[ -n "${LOG_FILE:-}" ]]; then
    candidates+=("$LOG_FILE")
  fi

  if [[ -n "$sudo_home" ]]; then
    candidates+=("${sudo_home}/${INSTALL_LOG_NAME}")
  fi

  candidates+=("${HOME}/${INSTALL_LOG_NAME}")
  candidates+=("/root/${INSTALL_LOG_NAME}")
  candidates+=("/var/log/${INSTALL_LOG_NAME}")
  candidates+=("/var/log/wsprrypi/${INSTALL_LOG_NAME}")

  for candidate in "${candidates[@]}"; do
    if [[ -f "$candidate" ]]; then
      found=1
      {
        echo "Source: $candidate"
        echo
        if [[ "$INCLUDE_FULL_LOGS" -eq 1 ]]; then
          cat "$candidate"
        else
          tail -n 1000 "$candidate"
        fi
      } > "${dest_dir}/$(basename "$candidate").txt" 2>/dev/null || true
    fi
  done

  if [[ "$found" -eq 0 ]]; then
    {
      echo "No WsprryPi installer log was found."
      echo
      echo "Checked candidates:"
      for candidate in "${candidates[@]}"; do
        echo "- $candidate"
      done
      echo
      echo "The installer defaults LOG_FILE to USER_HOME/wsprrypi.log unless LOG_FILE is supplied in the environment."
    } > "${dest_dir}/install-log-not-found.txt"
  fi
}

redact_file_in_place() {
  local file="$1"
  [[ -f "$file" ]] || return 0

  perl -0pi -e '
    s#([A-Za-z][A-Za-z0-9+.-]*://)[^/\s:@]+:[^/\s@]+@#${1}[REDACTED]@#g;
    s#("(?:password|pass|passwd|token|secret|api[_-]?key|access[_-]?key|upload[_-]?key|wsprnet[_-]?password|reporter[_-]?password)"\s*:\s*")[^"]*(")#${1}[REDACTED]${2}#gi;
    s#((?:password|pass|passwd|token|secret|api[_-]?key|access[_-]?key|upload[_-]?key|wsprnet[_-]?password|reporter[_-]?password)\s*[:=]\s*)\S+#${1}[REDACTED]#gi;
    s#((?:--?)(?:password|pass|passwd|token|secret|api[_-]?key|access[_-]?key|upload[_-]?key|wsprnet[_-]?password|reporter[_-]?password)\s+)\S+#${1}[REDACTED]#gi;
  ' "$file" 2>/dev/null || true
}

redact_tree() {
  find "$OUT_DIR" -type f \( \
    -name "*.txt" -o \
    -name "*.log" -o \
    -name "*.json" -o \
    -name "*.yaml" -o \
    -name "*.yml" -o \
    -name "*.conf" -o \
    -name "*.ini" -o \
    -name "*.service" \
  \) -print0 |
    while IFS= read -r -d '' file; do
      redact_file_in_place "$file"
    done
}

availability_reason() {
  local error_file="$1"
  if grep -qi 'permission denied' "$error_file" 2>/dev/null; then
    printf 'permission denied'
  elif grep -qiE 'no such file|not found' "$error_file" 2>/dev/null; then
    printf 'file unavailable'
  else
    printf 'unavailable'
  fi
}

capture_optional_command() {
  local output="$1" command_name="$2"
  shift 2
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf 'Collection status: command unavailable (%s)\n' "$command_name" > "$output"
    return
  fi
  "$@" > "$output" 2>&1
  local status=$?
  if [[ "$status" -ne 0 ]]; then
    printf '\nCollection status: command unavailable or failed (exit status %s)\n' "$status" >> "$output"
  fi
}

proc_start_time() {
  sed -E 's/^[0-9]+ \(.*\) //' "$1" 2>/dev/null | awk '{print $20}'
}

capture_proc_file() {
  local source="$1" output="$2" error_file
  error_file="${output}.error"
  if cat "$source" > "$output" 2> "$error_file"; then
    rm -f "$error_file"
    return 0
  fi
  local reason
  reason="$(availability_reason "$error_file")"
  {
    printf 'Collection status: %s\n' "$reason"
    [[ -s "$error_file" ]] && cat "$error_file"
  } > "$output"
  rm -f "$error_file"
  return 1
}

count_proc_entries() {
  local directory="$1" output_variable="$2" error_file result
  error_file="${OUT_DIR}/processes/.count-error.$$"
  if result="$(find "$directory" -mindepth 1 -maxdepth 1 -printf '.' 2> "$error_file")"; then
    printf -v "$output_variable" '%s' "${#result}"
    rm -f "$error_file"
    return 0
  fi
  printf -v "$output_variable" '%s' "unavailable ($(availability_reason "$error_file"))"
  rm -f "$error_file"
  return 1
}

status_value() {
  local field="$1" file="$2" value
  value="$(awk -F: -v field="$field" '$1 == field {sub(/^[[:space:]]+/, "", $2); print $2; exit}' "$file" 2>/dev/null)"
  [[ -n "$value" ]] && printf '%s' "$value" || printf 'unavailable'
}

smaps_value() {
  status_value "$1" "$2"
}

collect_process_snapshot() {
  local process_dir="${OUT_DIR}/processes"
  local active_state main_pid initial_start final_start task_count fd_count cmdline_tmp
  local active_error="${process_dir}/.active-state-error.txt"
  local main_pid_error="${process_dir}/.main-pid-error.txt"
  local detailed_status="unavailable"

  capture_optional_command "${process_dir}/all-processes.txt" ps ps -ww -eo pid,ppid,user,stat,etime,rss,vsz,nlwp,comm,args
  capture_optional_command "${process_dir}/process-tree.txt" pstree pstree -alp
  capture_optional_command "${process_dir}/systemd-cgroups.txt" systemd-cgls systemd-cgls --all --no-pager

  if ! command -v systemctl >/dev/null 2>&1; then
    active_state="unavailable"
    main_pid=""
    detailed_status="systemd command unavailable"
  else
    if ! systemctl show wsprrypi --property=ActiveState --value > "${process_dir}/.active-state.txt" 2> "$active_error"; then
      active_state="unavailable ($(availability_reason "$active_error"))"
    else
      active_state="$(cat "${process_dir}/.active-state.txt")"
    fi
    if ! systemctl show wsprrypi --property=MainPID --value > "${process_dir}/.main-pid.txt" 2> "$main_pid_error"; then
      main_pid=""
      detailed_status="MainPID unavailable ($(availability_reason "$main_pid_error"))"
    else
      main_pid="$(cat "${process_dir}/.main-pid.txt")"
    fi
    if [[ "$detailed_status" == MainPID\ unavailable* ]]; then
      :
    elif [[ ! "$main_pid" =~ ^[0-9]+$ ]]; then
      detailed_status="invalid or empty MainPID"
    elif [[ "$main_pid" == "0" ]]; then
      if [[ "$active_state" == "inactive" || "$active_state" == "failed" ]]; then
        detailed_status="service not running"
      else
        detailed_status="MainPID is zero; no active main process"
      fi
    elif [[ ! -d "/proc/${main_pid}" ]]; then
      detailed_status="process missing before detailed collection"
    else
      if capture_proc_file "/proc/${main_pid}/stat" "${process_dir}/wsprrypi-stat.txt"; then
        initial_start="$(proc_start_time "${process_dir}/wsprrypi-stat.txt")"
        if [[ ! "$initial_start" =~ ^[0-9]+$ ]]; then
          detailed_status="process identity unavailable"
        else
          capture_proc_file "/proc/${main_pid}/status" "${process_dir}/.status-full.txt" || true
          if grep -q '^Name:' "${process_dir}/.status-full.txt" 2>/dev/null; then
            awk -F: '$1 ~ /^(Name|State|Pid|PPid|Uid|Gid|VmPeak|VmSize|VmHWM|VmRSS|RssAnon|RssFile|RssShmem|Threads|voluntary_ctxt_switches|nonvoluntary_ctxt_switches)$/ {print}' \
              "${process_dir}/.status-full.txt" > "${process_dir}/wsprrypi-status.txt"
          else
            cp "${process_dir}/.status-full.txt" "${process_dir}/wsprrypi-status.txt"
          fi
          capture_proc_file "/proc/${main_pid}/smaps_rollup" "${process_dir}/wsprrypi-smaps-rollup.txt" || true
          capture_proc_file "/proc/${main_pid}/statm" "${process_dir}/wsprrypi-statm.txt" || true
          capture_proc_file "/proc/${main_pid}/limits" "${process_dir}/wsprrypi-limits.txt" || true
          capture_proc_file "/proc/${main_pid}/cgroup" "${process_dir}/wsprrypi-cgroup.txt" || true
          cmdline_tmp="${process_dir}/.cmdline-raw.txt"
          if capture_proc_file "/proc/${main_pid}/cmdline" "$cmdline_tmp"; then
            tr '\0' ' ' < "$cmdline_tmp" > "${process_dir}/wsprrypi-cmdline.txt"
            printf '\n' >> "${process_dir}/wsprrypi-cmdline.txt"
          else
            cp "$cmdline_tmp" "${process_dir}/wsprrypi-cmdline.txt"
          fi
          count_proc_entries "/proc/${main_pid}/task" task_count || true
          count_proc_entries "/proc/${main_pid}/fd" fd_count || true

          if cat "/proc/${main_pid}/stat" > "${process_dir}/.stat-final.txt" 2>/dev/null; then
            final_start="$(proc_start_time "${process_dir}/.stat-final.txt")"
            if [[ "$final_start" != "$initial_start" ]]; then
              detailed_status="process identity changed; PID may have been reused"
            else
              detailed_status="collected successfully"
            fi
          else
            detailed_status="process disappeared during collection"
          fi
        fi
      else
        detailed_status="process disappeared or stat was unreadable before detailed collection"
      fi
    fi
  fi

  {
    printf 'Collection status: %s\n' "$detailed_status"
    printf 'Service ActiveState: %s\n' "${active_state:-unavailable}"
    printf 'Systemd MainPID: %s\n' "${main_pid:-unavailable}"
    if [[ "$detailed_status" == "collected successfully" ]]; then
      printf 'Process start time (clock ticks since boot): %s\n' "$initial_start"
      printf 'VmRSS: %s\n' "$(status_value VmRSS "${process_dir}/wsprrypi-status.txt")"
      printf 'VmSize: %s\n' "$(status_value VmSize "${process_dir}/wsprrypi-status.txt")"
      printf 'PSS: %s\n' "$(smaps_value Pss "${process_dir}/wsprrypi-smaps-rollup.txt")"
      printf 'Threads (status): %s\n' "$(status_value Threads "${process_dir}/wsprrypi-status.txt")"
      printf 'Task directory count: %s\n' "${task_count:-unavailable}"
      printf 'Open file descriptor count: %s\n' "${fd_count:-unavailable}"
    else
      printf 'VmRSS: unavailable\nVmSize: unavailable\nPSS: unavailable\n'
      printf 'Threads (status): unavailable\nTask directory count: unavailable\nOpen file descriptor count: unavailable\n'
    fi
  } > "${process_dir}/wsprrypi-summary.txt"

  local artifact
  for artifact in wsprrypi-status.txt wsprrypi-smaps-rollup.txt wsprrypi-stat.txt \
    wsprrypi-statm.txt wsprrypi-limits.txt wsprrypi-cgroup.txt wsprrypi-cmdline.txt; do
    if [[ ! -f "${process_dir}/${artifact}" ]]; then
      printf 'Collection status: not collected (%s)\n' "$detailed_status" > "${process_dir}/${artifact}"
    fi
  done

  rm -f "${process_dir}/.status-full.txt" "${process_dir}/.cmdline-raw.txt" \
    "${process_dir}/.stat-final.txt" "${process_dir}/.count-error.$$" \
    "${process_dir}/.active-state.txt" "$active_error" "${process_dir}/.main-pid.txt" "$main_pid_error"
}

detect_project_path() {
  if [[ -n "$PROJECT_PATH" && -d "$PROJECT_PATH" ]]; then
    echo "$PROJECT_PATH"
    return
  fi

  for candidate in \
    "$PWD" \
    "$HOME/WsprryPi" \
    "$HOME/Wsprry_Pi" \
    "$HOME/GitHub/WsprryPi" \
    "$HOME/GitHub/Wsprry_Pi" \
    "/opt/WsprryPi" \
    "/opt/wsprrypi" \
    "/usr/local/src/WsprryPi" \
    "/usr/local/src/wsprrypi"
  do
    if [[ -d "$candidate/.git" ]] || [[ -f "$candidate/CMakeLists.txt" ]] || [[ -f "$candidate/config/wsprrypi.ini" ]]; then
      echo "$candidate"
      return
    fi
  done

  echo ""
}

ini_value() {
  local file="$1"
  local section="$2"
  local key="$3"
  local fallback="$4"

  if [[ ! -f "$file" ]]; then
    echo "$fallback"
    return
  fi

  awk -v section="$section" -v key="$key" -v fallback="$fallback" '
    BEGIN { in_section = 0; found = "" }
    /^[[:space:]]*;/ { next }
    /^[[:space:]]*#/ { next }
    /^[[:space:]]*\[/ {
      current = $0
      gsub(/^[[:space:]]*\[/, "", current)
      gsub(/\][[:space:]]*$/, "", current)
      in_section = (tolower(current) == tolower(section))
      next
    }
    in_section {
      line = $0
      split(line, parts, "=")
      candidate = parts[1]
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", candidate)
      if (tolower(candidate) == tolower(key)) {
        value = substr(line, index(line, "=") + 1)
        sub(/[;#].*$/, "", value)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
        found = value
      }
    }
    END { print found != "" ? found : fallback }
  ' "$file" 2>/dev/null
}

log "Collecting ${PROJECT_NAME} support information..."

cat > "${OUT_DIR}/README.txt" <<EOF
${PROJECT_NAME} Support Bundle
Generated UTC: ${STAMP}

This archive is intended for troubleshooting with the WsprryPi Support GPT or project maintainer.

Do not post it publicly unless you have reviewed the contents and are comfortable sharing them.

Bundle contents may include:
- Raspberry Pi and Linux system information, including 32/64-bit OS and CPU details
- WsprryPi installed runtime metadata and binary architecture checks
- WsprryPi project/git metadata when a checkout is detected
- Installed and project WsprryPi INI files, redacted
- Installed systemd unit, merged systemctl cat output, and service directive inspection
- Journald logs for WsprryPi and Apache services
- A point-in-time system process/tree/cgroup snapshot and WsprryPi memory, task, and file-descriptor counts
- Installer log if present, usually ~/wsprrypi.log from scripts/install.sh
- Legacy /var/log/wsprrypi logs only when present
- GPIO, I2C, Si5351-adjacent, band-switching, boot, and timing diagnostics
- Apache/web UI service diagnostics, enabled site config, document roots, and /var/www inventory when present

This bundle should not include passwords or upload credentials.
Common credential fields have been redacted automatically.
EOF

cat > "${OUT_DIR}/NEXT-STEPS.txt" <<EOF
${PROJECT_NAME} Support Bundle Created

This archive contains diagnostic information that may help troubleshoot a WsprryPi problem.

Recommended next step:
- Upload this .tar.gz archive to the WsprryPi Support GPT or share it with the project maintainer only if requested.

Do not post this archive publicly unless you have reviewed it and are comfortable sharing its contents.

No data has been uploaded automatically by this script.
EOF

if [[ -n "$CASE_ID" ]]; then
  cat >> "${OUT_DIR}/README.txt" <<EOF

Private intake case: ${CASE_ID}

This .tar.gz is the readable review copy. Do not attach it to a public issue or
upload it through the private intake channel. After review and explicit
approval, WsprryPi will encrypt these exact archive bytes for private upload.
EOF
  cat > "${OUT_DIR}/NEXT-STEPS.txt" <<EOF
${PROJECT_NAME} Private Support Candidate Created

Case ID: ${CASE_ID}

1. Download and inspect this readable .tar.gz candidate.
2. Reject and recollect if its contents or collection choices are unsuitable.
3. Approve it only when you are comfortable sharing the reviewed contents.
4. Upload only the later encrypted .age artifact through private intake.

Do not attach this readable archive to a public GitHub issue.
No data has been uploaded automatically by this script.
EOF
fi

PROJECT_PATH="$(detect_project_path)"

{
  echo "Detected project path: ${PROJECT_PATH:-not found}"
  echo "Collector working directory: $PWD"
  echo "Collector user: $(id 2>/dev/null || true)"
  echo "SUDO_USER: ${SUDO_USER:-}"
  echo "Hostname: $(hostname 2>/dev/null || true)"
  echo "Generated UTC: $STAMP"
  if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    PRIVILEGED_DIAGNOSTICS_INCOMPLETE=1
    echo
    echo "Note: collector is not running as root. Some system logs, service details, GPIO debug files, or boot files may be unavailable."
  fi
} > "${OUT_DIR}/project/detection.txt"

log "Collecting system summary..."

run_cmd uname uname -a
run_cmd os_release cat /etc/os-release
run_cmd hostnamectl hostnamectl
run_cmd date_local date
run_cmd date_utc date -u
run_cmd uptime uptime
run_cmd disk_free df -h
run_cmd memory free -h
run_cmd mounts mount
run_cmd current_user id
run_cmd groups groups
run_cmd shell_bit_width getconf LONG_BIT

if command -v lscpu >/dev/null 2>&1; then
  run_cmd lscpu lscpu
fi

if command -v dpkg >/dev/null 2>&1; then
  run_cmd dpkg_architecture dpkg --print-architecture
  run_cmd dpkg_foreign_architectures dpkg --print-foreign-architectures
fi

if command -v file >/dev/null 2>&1; then
  run_cmd bin_sh_architecture file /bin/sh
  run_cmd bin_bash_architecture file /bin/bash
fi

copy_if_exists /proc/cpuinfo "${OUT_DIR}/system"
copy_if_exists /proc/meminfo "${OUT_DIR}/system"
copy_if_exists /proc/device-tree/model "${OUT_DIR}/system"
copy_if_exists /proc/device-tree/compatible "${OUT_DIR}/system"
copy_if_exists /proc/cmdline "${OUT_DIR}/system"
copy_if_exists /etc/os-release "${OUT_DIR}/system"

if command -v vcgencmd >/dev/null 2>&1; then
  run_cmd vcgencmd_version vcgencmd version
  run_cmd vcgencmd_throttled vcgencmd get_throttled
  run_cmd vcgencmd_temp vcgencmd measure_temp
  run_cmd vcgencmd_volts_core vcgencmd measure_volts core
  run_cmd vcgencmd_clock_arm vcgencmd measure_clock arm
  run_cmd vcgencmd_mem_arm vcgencmd get_mem arm
  run_cmd vcgencmd_mem_gpu vcgencmd get_mem gpu
  run_cmd vcgencmd_bootloader_version vcgencmd bootloader_version
  run_cmd vcgencmd_bootloader_config vcgencmd bootloader_config
fi

log "Collecting project metadata..."

if [[ -n "$PROJECT_PATH" && -d "$PROJECT_PATH" ]]; then
  {
    echo "Project path: $PROJECT_PATH"
    echo
    find "$PROJECT_PATH" -maxdepth 3 -type f \
      \( -name "README*" -o -name "CMakeLists.txt" -o -name "Makefile" -o -name "*.service" -o -name "*.ini" -o -name "*.conf" \) \
      -print 2>/dev/null
  } > "${OUT_DIR}/project/project-files.txt"

  if [[ -d "$PROJECT_PATH/.git" ]]; then
    (
      cd "$PROJECT_PATH" || exit 0
      git remote -v
      echo
      git status --short --branch
      echo
      git rev-parse HEAD
      echo
      git log -1 --decorate --oneline
    ) > "${OUT_DIR}/project/git.txt" 2>&1 || true
  fi

  copy_if_exists "$PROJECT_PATH/Makefile" "${OUT_DIR}/project"
  copy_if_exists "$PROJECT_PATH/CMakeLists.txt" "${OUT_DIR}/project"
  copy_if_exists "$PROJECT_PATH/config/wsprrypi.ini" "${OUT_DIR}/project"
  copy_if_exists "$PROJECT_PATH/systemd/generic.service" "${OUT_DIR}/project"
  copy_if_exists "$PROJECT_PATH/systemd/systemd.md" "${OUT_DIR}/project"

  if [[ "$INCLUDE_CONFIGS" -eq 1 ]]; then
    mkdir -p "${OUT_DIR}/configs/project"
    find "$PROJECT_PATH" -maxdepth 4 -type f \( \
      -name "*.conf" -o \
      -name "*.ini" -o \
      -name "*.json" -o \
      -name "*.yaml" -o \
      -name "*.yml" -o \
      -name "*.service" \
    \) -print0 2>/dev/null |
      while IFS= read -r -d '' file; do
        rel="${file#"$PROJECT_PATH"/}"
        mkdir -p "${OUT_DIR}/configs/project/$(dirname "$rel")"
        cp "$file" "${OUT_DIR}/configs/project/$rel" 2>/dev/null || true
      done
  fi
else
  echo "Project path was not detected. Re-run with --path /path/to/WsprryPi if needed." > "${OUT_DIR}/project/project-not-found.txt"
fi

log "Collecting installed WsprryPi runtime information..."

if [[ "$INCLUDE_CONFIGS" -eq 1 ]]; then
  copy_if_exists "$INSTALLED_INI" "${OUT_DIR}/configs/installed"
fi
copy_if_exists "$INSTALLED_SERVICE" "${OUT_DIR}/configs/systemd"

if [[ -f "$INSTALLED_SERVICE" ]]; then
  {
    echo "Installed service file: $INSTALLED_SERVICE"
    echo
    echo "Key service directives:"
    grep -E '^[[:space:]]*(ExecStart|User|Group|WorkingDirectory|Environment|EnvironmentFile|SyslogIdentifier|StandardOutput|StandardError|Restart|RestartSec|Wants|Requires|After|Before|WantedBy)=' "$INSTALLED_SERVICE" 2>/dev/null || true
    echo
    echo "Referenced WsprryPi paths:"
    grep -Eo '(/usr/local/bin/wsprrypi(_debug)?|/usr/local/etc/wsprrypi\.ini|/[^[:space:]]*wsprrypi[^[:space:]]*)' "$INSTALLED_SERVICE" 2>/dev/null | sort -u || true
  } > "${OUT_DIR}/configs/systemd/wsprrypi-service-inspection.txt"
else
  echo "$INSTALLED_SERVICE was not found." > "${OUT_DIR}/configs/systemd/wsprrypi-service-not-found.txt"
fi

if [[ -x "$INSTALLED_EXE" ]]; then
  run_cmd wsprrypi_version "$INSTALLED_EXE" --version
  run_cmd wsprrypi_help "$INSTALLED_EXE" --help
  run_cmd wsprrypi_file file "$INSTALLED_EXE"
  run_cmd wsprrypi_stat stat "$INSTALLED_EXE"
  if command -v readelf >/dev/null 2>&1; then
    run_cmd wsprrypi_readelf_header readelf -h "$INSTALLED_EXE"
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    run_cmd wsprrypi_sha256 sha256sum "$INSTALLED_EXE"
  elif command -v shasum >/dev/null 2>&1; then
    run_cmd wsprrypi_sha256 shasum -a 256 "$INSTALLED_EXE"
  fi
  if command -v ldd >/dev/null 2>&1; then
    run_cmd wsprrypi_ldd ldd "$INSTALLED_EXE"
  fi
else
  echo "$INSTALLED_EXE was not found or is not executable." > "${OUT_DIR}/project/installed-runtime-not-found.txt"
fi

if [[ -x "$INSTALLED_DEBUG_EXE" ]]; then
  run_cmd wsprrypi_debug_version "$INSTALLED_DEBUG_EXE" --version
  run_cmd wsprrypi_debug_help "$INSTALLED_DEBUG_EXE" --help
  run_cmd wsprrypi_debug_file file "$INSTALLED_DEBUG_EXE"
  run_cmd wsprrypi_debug_stat stat "$INSTALLED_DEBUG_EXE"
  if command -v readelf >/dev/null 2>&1; then
    run_cmd wsprrypi_debug_readelf_header readelf -h "$INSTALLED_DEBUG_EXE"
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    run_cmd wsprrypi_debug_sha256 sha256sum "$INSTALLED_DEBUG_EXE"
  elif command -v shasum >/dev/null 2>&1; then
    run_cmd wsprrypi_debug_sha256 shasum -a 256 "$INSTALLED_DEBUG_EXE"
  fi
fi

log "Collecting systemd service status and journald logs..."

systemctl list-units --type=service --all > "${OUT_DIR}/system/systemd-services.txt" 2>&1 || true
systemctl cat wsprrypi > "${OUT_DIR}/configs/systemd/systemctl-cat-wsprrypi.txt" 2>&1 || true
systemctl show wsprrypi --no-pager > "${OUT_DIR}/configs/systemd/systemctl-show-wsprrypi.txt" 2>&1 || true
systemctl show wsprrypi --no-pager --property=Id,Names,LoadState,ActiveState,SubState,FragmentPath,DropInPaths,UnitFileState,ExecStart,User,Group,WorkingDirectory,Environment,StandardOutput,StandardError,SyslogIdentifier,Restart,RestartUSec,Wants,Requires,After,Before > "${OUT_DIR}/configs/systemd/systemctl-show-wsprrypi-summary.txt" 2>&1 || true
systemctl is-enabled wsprrypi > "${OUT_DIR}/logs/systemd-enabled-wsprrypi.txt" 2>&1 || true
systemctl is-active wsprrypi > "${OUT_DIR}/logs/systemd-active-wsprrypi.txt" 2>&1 || true

log "Collecting process and resource snapshot..."

collect_process_snapshot

for service in "${SERVICE_NAMES[@]}"; do
  systemctl status "$service" --no-pager > "${OUT_DIR}/logs/systemd-status-${service}.txt" 2>&1 || true

  if [[ "$INCLUDE_FULL_LOGS" -eq 1 ]]; then
    journalctl -u "$service" --no-pager > "${OUT_DIR}/logs/journal-unit-${service}.txt" 2>&1 || true
  else
    journalctl -u "$service" --no-pager -n 500 > "${OUT_DIR}/logs/journal-unit-${service}.txt" 2>&1 || true
  fi
done

for identifier in "${SYSLOG_IDENTIFIERS[@]}"; do
  if [[ "$INCLUDE_FULL_LOGS" -eq 1 ]]; then
    journalctl -t "$identifier" --no-pager > "${OUT_DIR}/logs/journal-identifier-${identifier}.txt" 2>&1 || true
  else
    journalctl -t "$identifier" --no-pager -n 500 > "${OUT_DIR}/logs/journal-identifier-${identifier}.txt" 2>&1 || true
  fi
done

log "Collecting general logs..."

if [[ "$INCLUDE_FULL_LOGS" -eq 1 ]]; then
  journalctl --no-pager > "${OUT_DIR}/logs/journal-system.txt" 2>&1 || true
else
  journalctl --no-pager -n 800 > "${OUT_DIR}/logs/journal-system-recent.txt" 2>&1 || true
fi

dmesg -T > "${OUT_DIR}/logs/dmesg.txt" 2>&1 || dmesg > "${OUT_DIR}/logs/dmesg.txt" 2>&1 || true

tail_or_copy_log /var/log/syslog "${OUT_DIR}/logs/syslog"
tail_or_copy_log /var/log/messages "${OUT_DIR}/logs/messages"

if [[ -d "$LEGACY_LOG_DIR" ]]; then
  find "$LEGACY_LOG_DIR" -maxdepth 2 -type f -print0 2>/dev/null |
    while IFS= read -r -d '' file; do
      rel="${file#"$LEGACY_LOG_DIR"/}"
      tail_or_copy_log "$file" "${OUT_DIR}/logs/legacy-wsprrypi/$rel"
    done
else
  echo "$LEGACY_LOG_DIR was not found; current WsprryPi service versions are expected to log to journald." > "${OUT_DIR}/logs/legacy-wsprrypi-log-dir-not-found.txt"
fi

collect_install_logs

log "Collecting hardware / GPIO / I2C diagnostics..."

run_cmd dev_nodes sh -c 'ls -la /dev/i2c-* /dev/gpiochip* /dev/gpiomem* /dev/mem 2>/dev/null || true'
run_cmd loaded_modules sh -c 'lsmod 2>/dev/null | grep -E "(^i2c|gpio|bcm|pwm|clk|spi|gpiomem)" || true'
run_cmd gpio_groups getent group gpio i2c spi dialout

if command -v raspi-config >/dev/null 2>&1; then
  run_cmd raspi_config_nonint_i2c raspi-config nonint get_i2c
fi

if command -v i2cdetect >/dev/null 2>&1; then
  run_cmd i2cdetect_list i2cdetect -l
  if [[ "$PROBE_I2C" -eq 1 ]]; then
    if i2cdetect -y 1 > "${OUT_DIR}/commands/i2cdetect_bus_1.txt" 2>&1; then
      I2C_PROBE_STATUS="succeeded"
    else
      I2C_SCAN_EXIT_STATUS=$?
      I2C_PROBE_STATUS="failed"
      echo "[command exited with status: $I2C_SCAN_EXIT_STATUS]" >> "${OUT_DIR}/commands/i2cdetect_bus_1.txt"
    fi
  else
    I2C_PROBE_STATUS="skipped_by_user"
    echo "Active I2C scan was skipped because --probe-i2c was not supplied." > "${OUT_DIR}/commands/i2cdetect_bus_1.txt"
  fi
else
  if [[ "$PROBE_I2C" -eq 1 ]]; then
    I2C_PROBE_STATUS="unavailable"
  else
    I2C_PROBE_STATUS="skipped_by_user"
  fi
  echo "i2cdetect is unavailable; no active I2C scan was run." > "${OUT_DIR}/commands/i2cdetect_bus_1.txt"
fi

if command -v gpioinfo >/dev/null 2>&1; then
  run_cmd gpioinfo gpioinfo
fi

if command -v gpiodetect >/dev/null 2>&1; then
  run_cmd gpiodetect gpiodetect
fi

if command -v gpiofind >/dev/null 2>&1; then
  run_cmd gpiofind_gpio4 gpiofind GPIO4
  run_cmd gpiofind_gpio20 gpiofind GPIO20
fi

if command -v pinout >/dev/null 2>&1; then
  run_cmd pinout pinout
fi

if command -v gpio >/dev/null 2>&1; then
  run_cmd wiringpi_gpio_readall gpio readall
fi

if [[ -r /sys/kernel/debug/gpio ]]; then
  copy_if_exists /sys/kernel/debug/gpio "${OUT_DIR}/hardware"
fi

copy_if_exists /boot/config.txt "${OUT_DIR}/configs/boot"
copy_if_exists /boot/firmware/config.txt "${OUT_DIR}/configs/boot"
copy_if_exists /boot/cmdline.txt "${OUT_DIR}/configs/boot"
copy_if_exists /boot/firmware/cmdline.txt "${OUT_DIR}/configs/boot"

log "Collecting Apache / web UI diagnostics..."

systemctl status apache2 --no-pager > "${OUT_DIR}/web/apache2-status.txt" 2>&1 || true
systemctl is-enabled apache2 > "${OUT_DIR}/web/apache2-enabled.txt" 2>&1 || true
systemctl is-active apache2 > "${OUT_DIR}/web/apache2-active.txt" 2>&1 || true

if command -v apache2ctl >/dev/null 2>&1; then
  run_cmd apache2ctl_configtest apache2ctl configtest
  run_cmd apache2ctl_sites apache2ctl -S
  run_cmd apache2ctl_modules apache2ctl -M
fi

if command -v ss >/dev/null 2>&1; then
  run_cmd listening_tcp ss -ltnp
elif command -v netstat >/dev/null 2>&1; then
  run_cmd listening_tcp netstat -ltnp
fi


copy_if_exists /etc/apache2/ports.conf "${OUT_DIR}/configs/apache2"
copy_if_exists /etc/apache2/sites-available "${OUT_DIR}/configs/apache2"
copy_if_exists /etc/apache2/sites-enabled "${OUT_DIR}/configs/apache2"
copy_if_exists /etc/apache2/conf-available "${OUT_DIR}/configs/apache2"
copy_if_exists /etc/apache2/conf-enabled "${OUT_DIR}/configs/apache2"

run_cmd apache_document_roots sh -c "grep -RhiE '^[[:space:]]*(DocumentRoot|Alias|ProxyPass|ProxyPassReverse|<Directory|DirectoryIndex)' /etc/apache2/sites-enabled /etc/apache2/sites-available /etc/apache2/conf-enabled /etc/apache2/conf-available 2>/dev/null || true"
run_cmd apache_enabled_config_tree sh -c "find /etc/apache2/sites-enabled /etc/apache2/conf-enabled -maxdepth 2 -printf '%M %u %g %s %TY-%Tm-%Td %TH:%TM %p -> %l\n' 2>/dev/null || true"
run_cmd var_www_tree sh -c "find /var/www -maxdepth 3 -printf '%M %u %g %s %TY-%Tm-%Td %TH:%TM %p -> %l\n' 2>/dev/null || true"

WEB_PORT="$(ini_value "$INSTALLED_INI" "Operation" "Web Port" "$DEFAULT_WEB_PORT")"
SOCKET_PORT="$(ini_value "$INSTALLED_INI" "Operation" "Socket Port" "$DEFAULT_SOCKET_PORT")"
{
  echo "Configured Web Port: $WEB_PORT"
  echo "Configured Socket Port: $SOCKET_PORT"
} > "${OUT_DIR}/web/wsprrypi-web-ports.txt"

if command -v curl >/dev/null 2>&1; then
  run_cmd wsprrypi_web_root_probe curl -fsS --max-time 3 "http://127.0.0.1:${WEB_PORT}/"
fi

log "Collecting network summary..."

if command -v ip >/dev/null 2>&1; then
  run_cmd ip_addr ip -brief addr
  run_cmd ip_route ip route
fi

log "Collecting timing / clock information..."

run_cmd timedatectl timedatectl

if command -v chronyc >/dev/null 2>&1; then
  run_cmd chronyc_tracking chronyc tracking
  run_cmd chronyc_sources chronyc sources -v
fi

if command -v ntpq >/dev/null 2>&1; then
  run_cmd ntpq_peers ntpq -p
fi

log "Collecting package information..."

log "Collecting RP1 GPCLK DKMS, module, runtime endpoint, and route state..."
mkdir -p "${OUT_DIR}/hardware/rp1-gpclk"
RP1_KERNEL="$(uname -r 2>/dev/null || true)"
if [[ -n "$RP1_KERNEL" ]]; then
  run_optional_absolute_cmd rp1_gpclk_dkms "$RP1_DKMS" status -m rp1-gpclk-dkms
  for rp1_module in rp1_gpclk_dkms rp1_route_controller; do
    for rp1_field in filename version vermagic; do
      run_optional_absolute_cmd \
        "rp1_gpclk_modinfo_${rp1_module}_${rp1_field}" \
        "$RP1_MODINFO" -k "$RP1_KERNEL" -F "$rp1_field" "$rp1_module"
    done
  done
  {
    echo "\$ test -d /usr/src/linux-headers-$RP1_KERNEL"
    echo
    if [[ -d "/usr/src/linux-headers-$RP1_KERNEL" ]]; then
      echo "Running-kernel headers present"
    else
      echo "Running-kernel headers MISSING"
    fi
  } > "${OUT_DIR}/commands/rp1_gpclk_running_kernel_headers.txt"
else
  for rp1_name in \
    rp1_gpclk_dkms \
    rp1_gpclk_running_kernel_headers \
    rp1_gpclk_modinfo_rp1_gpclk_dkms_filename \
    rp1_gpclk_modinfo_rp1_gpclk_dkms_version \
    rp1_gpclk_modinfo_rp1_gpclk_dkms_vermagic \
    rp1_gpclk_modinfo_rp1_route_controller_filename \
    rp1_gpclk_modinfo_rp1_route_controller_version \
    rp1_gpclk_modinfo_rp1_route_controller_vermagic; do
    echo "Collection status: running kernel unavailable" \
      > "${OUT_DIR}/commands/${rp1_name}.txt"
  done
fi
if [[ -r /proc/modules ]]; then
  # shellcheck disable=SC2016  # $1 is an awk field, not a shell parameter.
  run_cmd rp1_gpclk_loaded_modules awk \
    '$1 == "rp1_gpclk_dkms" || $1 == "rp1_route_controller" { print; found=1 } END { if (!found) print "Neither RP1 GPCLK module is loaded" }' \
    /proc/modules
else
  echo "Collection status: /proc/modules absent or inaccessible" \
    > "${OUT_DIR}/commands/rp1_gpclk_loaded_modules.txt"
fi
if [[ -f "$RP1_INSTALLATION_RECORD" ]]; then
  echo "WsprryPi installation record present" \
    > "${OUT_DIR}/hardware/rp1-gpclk/wsprrypi-installation-record.txt"
else
  echo "WsprryPi installation record absent or inaccessible" \
    > "${OUT_DIR}/hardware/rp1-gpclk/wsprrypi-installation-record.txt"
fi
collect_rp1_record_summary
if safe_root_owned_regular_file "$RP1_QUALIFICATION"; then
  run_bounded_absolute_cmd rp1_gpclk_qualification_evidence \
    "$RP1_PYTHON" -I "$RP1_QUALIFICATION" status
else
  echo "Qualification evidence interpreter absent, inaccessible, or unsafe" \
    > "${OUT_DIR}/commands/rp1_gpclk_qualification_evidence.txt"
fi
if safe_root_owned_regular_file "$RP1_RUNTIME_PROVIDER"; then
  run_bounded_absolute_cmd rp1_gpclk_runtime_provider_inspect \
    "$RP1_PYTHON" "$RP1_RUNTIME_PROVIDER" inspect
else
  {
    echo "\$ $RP1_PYTHON $RP1_RUNTIME_PROVIDER inspect"
    echo
    echo "Collection status: runtime provider absent, inaccessible, or unsafe ($RP1_RUNTIME_PROVIDER)"
  } > "${OUT_DIR}/commands/rp1_gpclk_runtime_provider_inspect.txt"
fi
run_cmd rp1_gpclk_device_state sh -c "if [ -e /dev/rp1-gpclk ]; then ls -l /dev/rp1-gpclk; else echo 'device unavailable'; fi"
run_cmd rp1_gpclk_route_socket systemctl show rp1-gpclk-route-manager.socket --property=ActiveState,UnitFileState,FragmentPath
run_cmd rp1_gpclk_route_journals sh -c "find /var/lib/rp1-gpclk-dkms/route-transactions -maxdepth 1 -type f -name '*.json' -printf '%f %u:%g %m %s bytes\n' 2>/dev/null | sort || true"
{
  echo "Provider provisioning and package identity: external to WsprryPi"
  echo "DKMS registration and kernel-specific module identity: captured as passive command reports"
  echo "WsprryPi ownership record: bounded identity whitelist; full record contents are not copied"
  echo "Runtime provider readiness: captured through the read-only inspect operation when available"
  echo "Persisted route: GPIO$(ini_value "$INSTALLED_INI" "GPIO" "Transmit Pin" "unavailable")"
  echo "Active route and operation readiness: reported through the runtime provider protocol"
  echo "Qualification: not established by this support bundle"
} > "${OUT_DIR}/hardware/rp1-gpclk/evidence-summary.txt"

if command -v dpkg >/dev/null 2>&1; then
  dpkg -l > "${OUT_DIR}/system/dpkg-list.txt" 2>&1 || true
fi

if command -v apt-cache >/dev/null 2>&1; then
  run_cmd apt_policy_git apt-cache policy git
  run_cmd apt_policy_apache2 apt-cache policy apache2
  run_cmd apt_policy_php apt-cache policy php
  run_cmd apt_policy_chrony apt-cache policy chrony
  run_cmd apt_policy_i2c_tools apt-cache policy i2c-tools
  run_cmd apt_policy_gpio_tools apt-cache policy gpiod
  run_cmd apt_policy_libgpiod_dev apt-cache policy libgpiod-dev
  run_cmd apt_policy_libsystemd_dev apt-cache policy libsystemd-dev
  run_cmd apt_policy_binutils apt-cache policy binutils
fi

log "Redacting sensitive fields..."

redact_tree

create_private_manifest() {
  local manifest="${OUT_DIR}/manifest.json" manifest_tmp="${OUT_ROOT}/manifest.json.tmp" created path relative size digest first=1
  [[ -n "$CASE_ID" ]] || return 0
  if find "$OUT_DIR" -type l -print -quit | grep -q .; then
    find "$OUT_DIR" -type l -delete || fail "Unable to omit support-bundle symbolic links."
    SYMLINKS_OMITTED=1
  fi
  if find "$OUT_DIR" -mindepth 1 ! -type d ! -type f -print -quit | grep -q .; then
    fail "Support bundle contains an unsupported filesystem node."
  fi
  if find "$OUT_DIR" -type f -links +1 -print -quit | grep -q .; then
    fail "Support bundle contains a multiply linked file."
  fi
  created="${STAMP:0:4}-${STAMP:4:2}-${STAMP:6:2}T${STAMP:9:2}:${STAMP:11:2}:${STAMP:13:2}Z"
  {
    printf '{\n'
    printf '  "schema_version": 1,\n'
    printf '  "contract_version": 1,\n'
    printf '  "project_id": "wsprrypi",\n'
    printf '  "project_version": "'; json_escape "$PROJECT_VERSION"; printf '",\n'
    printf '  "case_id": "%s",\n' "$CASE_ID"
    printf '  "created_at_utc": "%s",\n' "$created"
    printf '  "collection_options": {\n'
    printf '    "configuration_files_included": '; json_bool "$INCLUDE_CONFIGS"; printf ',\n'
    printf '    "full_logs_included": '; json_bool "$INCLUDE_FULL_LOGS"; printf ',\n'
    printf '    "i2c_probe_requested": '; json_bool "$PROBE_I2C"; printf '\n'
    printf '  },\n'
    printf '  "privacy_categories": ["callsign", "locator", "internal_ip", "logs"],\n'
    printf '  "support_context": {\n'
    printf '    "kind": "%s",\n' "$CONTEXT_KIND"
    if [[ "$CONTEXT_KIND" == "existing_github_issue" ]]; then
      printf '    "issue_url": "%s",\n' "$GITHUB_ISSUE_URL"
      printf '    "problem_description": null,\n'
      printf '    "contact": null\n'
    else
      printf '    "issue_url": null,\n'
      printf '    "problem_description": "'; json_escape "$PROBLEM_DESCRIPTION"; printf '",\n'
      printf '    "contact": "'; json_escape "$CONTACT_VALUE"; printf '"\n'
    fi
    printf '  },\n'
    printf '  "collection_warnings": ['
    first=1
    if [[ "$PRIVILEGED_DIAGNOSTICS_INCOMPLETE" -eq 1 ]]; then
      printf '"privileged_diagnostics_may_be_incomplete"'; first=0
    fi
    if [[ "$SYMLINKS_OMITTED" -eq 1 ]]; then
      [[ "$first" -eq 1 ]] || printf ', '
      printf '"symlinks_omitted"'
    fi
    printf '],\n'
    first=1
    printf '  "files": [\n'
    while IFS= read -r -d '' path; do
      [[ "$path" == "$manifest" ]] && continue
      relative="${path#"$OUT_DIR"/}"
      [[ -n "$relative" && "$relative" != "$path" && "$relative" != *\\* &&
         "$relative" != ".." && "$relative" != ../* && "$relative" != */../* &&
         "$relative" != */.. ]] || exit 1
      size="$(stat -c '%s' "$path")" || exit 1
      if command -v sha256sum >/dev/null 2>&1; then
        digest="$(sha256sum "$path" | awk '{print $1}')"
      else
        digest="$(shasum -a 256 "$path" | awk '{print $1}')"
      fi
      [[ "$size" =~ ^[0-9]+$ && "$digest" =~ ^[0-9a-f]{64}$ ]] || exit 1
      [[ "$first" -eq 1 ]] || printf ',\n'
      first=0
      printf '    {"path": "'; json_escape "$relative"; printf '", "size": %s, "sha256": "%s"}' "$size" "$digest"
    done < <(LC_ALL=C find "$OUT_DIR" -type f -print0 | LC_ALL=C sort -z)
    printf '\n  ]\n}\n'
  } > "$manifest_tmp" || { rm -f "$manifest_tmp"; fail "Unable to create private support manifest."; }
  chmod 600 "$manifest_tmp" || { rm -f "$manifest_tmp"; fail "Unable to secure private support manifest."; }
  ln "$manifest_tmp" "$manifest" || { rm -f "$manifest_tmp"; fail "Unable to publish private support manifest."; }
  rm -f "$manifest_tmp"
  MANIFEST_INCLUDED=1
}

create_private_manifest

log "Creating archive..."

ARCHIVE_TMP="${ARCHIVE}.tmp.$$"
SHA256_TMP="${SHA256_FILE}.tmp.$$"
rm -f "$ARCHIVE_TMP" "$SHA256_TMP"
(
  cd "$OUT_ROOT" || exit 1
  tar -czf "$ARCHIVE_TMP" bundle
) || fail "Failed to create support bundle archive."

[[ -f "$ARCHIVE_TMP" ]] || fail "Failed to create support bundle archive."
chmod 600 "$ARCHIVE_TMP" || fail "Unable to secure support bundle archive."

SHA256_DIGEST=""
if command -v sha256sum >/dev/null 2>&1; then
  SHA256_DIGEST="$(sha256sum "$ARCHIVE_TMP" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
  SHA256_DIGEST="$(shasum -a 256 "$ARCHIVE_TMP" | awk '{print $1}')"
else
  fail "No SHA-256 command is available."
fi
[[ "$SHA256_DIGEST" =~ ^[[:xdigit:]]{64}$ ]] || fail "Unable to calculate support bundle SHA-256."
printf '%s  %s\n' "$SHA256_DIGEST" "$ARCHIVE_NAME" > "$SHA256_TMP" || fail "Unable to create SHA-256 sidecar."
chmod 600 "$SHA256_TMP" || fail "Unable to secure SHA-256 sidecar."

ln "$ARCHIVE_TMP" "$ARCHIVE" || fail "Refusing to overwrite an existing support-bundle archive."
rm -f "$ARCHIVE_TMP"
ln "$SHA256_TMP" "$SHA256_FILE" || { rm -f "$ARCHIVE"; fail "Refusing to overwrite an existing SHA-256 sidecar."; }
rm -f "$SHA256_TMP"
publish_result success "$ARCHIVE_NAME" "$(basename "$SHA256_FILE")" "$SHA256_DIGEST" || {
  rm -f "$ARCHIVE" "$SHA256_FILE"
  fail "Unable to publish support bundle result."
}

echo
echo "Created support bundle:"
echo "  $ARCHIVE"
echo
echo "SHA256:"
echo "  $SHA256_DIGEST"
echo
echo "Share this .tar.gz file with the WsprryPi Support GPT or maintainer if requested."
echo "Do not post it publicly unless you have reviewed its contents."
