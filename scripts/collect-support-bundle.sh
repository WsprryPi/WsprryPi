#!/usr/bin/env bash
set -u
set -o pipefail

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

STAMP="$(date -u +"%Y%m%dT%H%M%SZ")"
HOST="$(hostname -s 2>/dev/null || echo raspberrypi)"
OUT_ROOT="${TMPDIR:-/tmp}/${PROJECT_NAME}-support-${STAMP}"
OUT_DIR="${OUT_ROOT}/bundle"
ARCHIVE="${PWD}/${PROJECT_NAME}-support-${HOST}-${STAMP}.tar.gz"

INCLUDE_CONFIGS=1
INCLUDE_FULL_LOGS=0
KEEP_WORKDIR=0
PROJECT_PATH=""

usage() {
  cat <<EOF
${PROJECT_NAME} support bundle collector

Creates a local .tar.gz support bundle for WsprryPi troubleshooting.

Usage:
  curl -fsSL <url>/collect-support-bundle.sh | bash
  curl -fsSL <url>/collect-support-bundle.sh | bash -s -- [options]

Options:
  --path PATH          WsprryPi checkout/install path
  --no-configs         Do not include redacted config files
  --full-logs          Include larger journal/log output
  --keep-workdir       Keep temporary collection directory
  -h, --help           Show this help

Notes:
  - No data is uploaded by this script.
  - Passwords, tokens, upload secrets, and common credential fields are redacted.
  - This script is intended for Raspberry Pi / Linux WsprryPi systems.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --path)
      shift
      PROJECT_PATH="${1:-}"
      ;;
    --no-configs) INCLUDE_CONFIGS=0 ;;
    --full-logs) INCLUDE_FULL_LOGS=1 ;;
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

mkdir -p "$OUT_DIR"/{system,project,logs,configs,hardware,web,network,commands}

cleanup() {
  if [[ "$KEEP_WORKDIR" -eq 0 ]]; then
    rm -rf "$OUT_ROOT"
  else
    echo "Temporary work directory kept at: $OUT_ROOT"
  fi
}
trap cleanup EXIT

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

PROJECT_PATH="$(detect_project_path)"

{
  echo "Detected project path: ${PROJECT_PATH:-not found}"
  echo "Collector working directory: $PWD"
  echo "Collector user: $(id 2>/dev/null || true)"
  echo "SUDO_USER: ${SUDO_USER:-}"
  echo "Hostname: $(hostname 2>/dev/null || true)"
  echo "Generated UTC: $STAMP"
  if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
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
        rel="${file#$PROJECT_PATH/}"
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
      rel="${file#$LEGACY_LOG_DIR/}"
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
  run_cmd i2cdetect_bus_1 i2cdetect -y 1
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

log "Creating archive..."

(
  cd "$OUT_ROOT" || exit 1
  tar -czf "$ARCHIVE" bundle
)

if [[ -f "$ARCHIVE" ]]; then
  sha256sum "$ARCHIVE" > "${ARCHIVE}.sha256" 2>/dev/null || shasum -a 256 "$ARCHIVE" > "${ARCHIVE}.sha256" 2>/dev/null || true

  echo
  echo "Created support bundle:"
  echo "  $ARCHIVE"

  if [[ -f "${ARCHIVE}.sha256" ]]; then
    echo
    echo "SHA256:"
    cat "${ARCHIVE}.sha256"
  fi

  echo
  echo "Share this .tar.gz file with the WsprryPi Support GPT or maintainer if requested."
  echo "Do not post it publicly unless you have reviewed its contents."
else
  echo "Failed to create support bundle." >&2
  exit 1
fi
