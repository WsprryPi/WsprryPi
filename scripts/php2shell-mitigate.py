#!/usr/bin/env python3
"""Audit/apply partial PHP2Shell mitigations in Debian's web PHP INI trees.

No PHP execution, HTTP probes, package changes, or service operations. See
scripts/README.md and https://www.php.net/archive/2026.php#2026-10-21-1.
"""

from __future__ import annotations

import argparse
from contextlib import redirect_stdout
from dataclasses import dataclass, field
from datetime import date, datetime, timezone
import io
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import textwrap
import uuid


NAME = "99-wsprrypi-php2shell.ini"
MARKER = "; Managed by WsprryPi php2shell-mitigate.py\n"
FIXED = {"8.2": "8.2.35", "8.3": "8.3.36", "8.4": "8.4.27", "8.5": "8.5.12"}
KEYS = {"session.upload_progress.enabled", "fatal_error_backtraces",
        "memory_limit", "post_max_size", "upload_max_filesize"}
SAPIS = ("apache2", "fpm", "cgi")
MIB = 1024 * 1024
# Release coverage is a conservative Debian baseline, not a vendor guarantee.
# Sources: debian.org/releases/{bookworm,trixie,bullseye}/ (reviewed 2026-10-21).
RELEASES = {"bookworm": ("12", date(2028, 6, 30)), "trixie": ("13", date(2030, 6, 30))}
PHP_EOL = {"8.2": date(2026, 12, 31), "8.3": date(2027, 12, 31),
           "8.4": date(2028, 12, 31), "8.5": date(2029, 12, 31)}
APT_CACHE_OPTIONS = ["-o", "Dir::Cache::pkgcache=", "-o", "Dir::Cache::srcpkgcache="]
PACKAGE_NAME = r"[a-z0-9][a-z0-9+.-]*(?::[a-z0-9-]+)?"


def read_config(path):
    # Application-owned .htaccess/.user.ini can be untrusted. Do not block on a
    # FIFO/device or read an unbounded file, including through a symlink.
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    with os.fdopen(descriptor, "rb") as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            raise ValueError(f"Configuration is not a regular file: {path}")
        contents = source.read(2 * MIB + 1)
    if len(contents) > 2 * MIB:
        raise ValueError(f"Configuration exceeds the 2 MiB inspection limit: {path}")
    return contents


def walk_error(error):
    raise error


@dataclass
class Plan:
    directory: Path
    settings: dict
    previous: bytes | None
    contents: bytes

    @property
    def target(self):
        return self.directory / "conf.d" / NAME


def uncomment(line):
    """PHP INI comments use semicolons outside quotes."""
    quote = None
    for index, char in enumerate(line):
        if char in "\"'" and (index == 0 or line[index - 1] != "\\"):
            quote = None if quote == char else char if quote is None else quote
        if char == ";" and quote is None:
            return line[:index].strip()
    return line.strip()


def ini_values(path):
    values, scoped = {}, []
    special_section = False
    for number, raw in enumerate(read_config(path).decode("utf-8").splitlines(), 1):
        line = uncomment(raw)
        if line.startswith("["):
            special_section = bool(re.match(r"\[\s*(?:PATH|HOST)\s*=", line, re.I))
        match = re.match(r"([\w.]+)\s*=\s*(.*)$", line)
        if match and match[1] in KEYS:
            if special_section:
                scoped.append(f"{path}:{number}: scoped {match[1]} (not included in global INI checks)")
            else:
                value = match[2]
                if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                    value = value[1:-1]
                values[match[1]] = value
    return values, scoped


def quantity(value):
    # PHP 8.2+ interprets leading-zero quantities as octal. Keep the static
    # audit conservative across old PHP parsers and never relax such a limit.
    match = re.fullmatch(r"(0|[1-9][0-9]*)\s*([KMG]?)", value or "", re.I)
    if not match:
        return None
    return int(match[1]) * 1024 ** ("", "K", "M", "G").index(match[2].upper())


def disabled(value):
    return value is not None and value.lower() in {"", "0", "off", "false", "no", "none"}


def discover(root, selected):
    php = root / "etc/php"
    if not php.is_dir():
        return []
    return [version / sapi for version in sorted(php.iterdir())
            if re.fullmatch(r"[5-8]\.\d+", version.name) and version.is_dir()
            for sapi in SAPIS if (not selected or sapi in selected)
            and (version / sapi).is_dir()]


def inspect(directory, allow_memory):
    if not (directory / "php.ini").is_file() or not (directory / "conf.d").is_dir():
        raise ValueError(f"Incomplete PHP configuration: {directory}")
    version = tuple(map(int, directory.parent.name.split(".")))
    if version < (5, 4) or version > (8, 5):
        raise ValueError(f"PHP {directory.parent.name}: outside this advisory's supported range")
    target = directory / "conf.d" / NAME
    if target.is_symlink():
        raise ValueError(f"Refusing a symlink at managed target: {target}")
    previous = read_config(target) if target.exists() else None
    if previous is not None and not previous.startswith(MARKER.encode()):
        raise ValueError(f"Refusing an existing file without the ownership marker: {target}")

    values, findings = {}, []
    for path in [directory / "php.ini", *sorted((directory / "conf.d").glob("*.ini"))]:
        current, scoped = ini_values(path)
        values.update(current)
        findings.extend(scoped)

    # Preserve stricter positive request limits. Zero means unlimited for POST.
    for key in ["post_max_size", "upload_max_filesize"]:
        if key in values and quantity(values[key]) is None:
            raise ValueError(f"Cannot safely preserve noncanonical {key} in {directory}; resolve expressions/environment/octal values to decimal bytes or K/M/G first")
    post = min(quantity(values.get("post_max_size")) or 8 * MIB, 8 * MIB)
    upload = min(quantity(values.get("upload_max_filesize")) or 2 * MIB, 2 * MIB, post // 2)
    if upload < 1:
        raise ValueError(f"POST limit is too small to set a positive lower upload limit: {directory}")
    settings = {"session.upload_progress.enabled": "0", "post_max_size": str(post),
                "upload_max_filesize": str(upload)}
    if version == (8, 5):
        settings["fatal_error_backtraces"] = "0"
    # A repeat apply must not silently undo a previous explicit memory opt-in.
    old_values = ini_values(target)[0] if previous is not None else {}
    if allow_memory or old_values.get("memory_limit") == "-1":
        settings["memory_limit"] = "-1"
    contents = (MARKER + "; Partial mitigations only; install vendor PHP security updates.\n"
                + "".join(f"{key} = {value}\n" for key, value in settings.items())).encode()
    return Plan(directory, settings, previous, contents), values, findings


def settings_findings(values, version):
    """Optional partial-mitigation checks; these do not establish patch status."""
    findings = []
    for key in ["session.upload_progress.enabled", *(["fatal_error_backtraces"] if version == "8.5" else [])]:
        if not disabled(values.get(key)):
            findings.append(f"{key}: needs 0 (currently {values.get(key, 'not explicitly set')})")
    post, upload = quantity(values.get("post_max_size")), quantity(values.get("upload_max_filesize"))
    if post is None or not 0 < post <= 8 * MIB:
        findings.append("post_max_size: needs a positive limit of at most 8 MiB")
    if upload is None or not upload > 0 or post is None or upload >= post:
        findings.append("upload_max_filesize: needs a positive limit below post_max_size")
    if values.get("memory_limit") != "-1":
        findings.append("memory_limit: finite or unknown; optional unlimited-memory mitigation is not configured")
    return findings


def override_findings(root, directories, web_roots):
    """Conservative inventory, including inactive configs; never print their values."""
    paths, findings = set(), []
    for base in [root / "etc/apache2", root / "etc/nginx", *[d / "pool.d" for d in directories if d.name == "fpm"]]:
        if base.is_dir():
            for current, dirs, files in os.walk(base, onerror=walk_error):
                for name in dirs:
                    if (Path(current) / name).is_symlink():
                        findings.append(f"{Path(current) / name}: config directory symlink not traversed; review separately")
                for name in files:
                    path = Path(current) / name
                    # Standard server configuration only. In particular, do
                    # not open private keys/certificates or arbitrary binaries.
                    if path.suffix in {".conf", ".load"} or name in {"fastcgi_params", "scgi_params", "uwsgi_params"} or (path.parent.name in {"sites-available", "sites-enabled"} and not path.suffix):
                        paths.add(path)
    for base in web_roots:
        if not base.is_dir():
            findings.append(f"{base}: web root unavailable; application overrides not inspected")
            continue
        for current, dirs, files in os.walk(base, onerror=walk_error):
            for name in dirs:
                if (Path(current) / name).is_symlink():
                    findings.append(f"{Path(current) / name}: symlink directory not traversed; review separately")
            for name in files:
                if name in {".htaccess", ".user.ini"}:
                    paths.add(Path(current) / name)
    seen = set()
    for path in sorted(paths):
        identity = path.resolve(strict=True)
        if identity in seen:
            continue
        seen.add(identity)
        for number, raw in enumerate(read_config(path).decode("utf-8").splitlines(), 1):
            line = raw.strip()
            if not line or line.startswith(("#", ";")):
                continue
            if re.search(r"\bphp_(?:admin_)?(?:value|flag)\b|\bPHP_(?:ADMIN_)?VALUE\b", line, re.I):
                findings.append(f"{path}:{number}: PHP override/diagnostic source (effect not evaluated)")
            elif path.name == ".user.ini" and "=" in line:
                findings.append(f"{path}:{number}: per-directory INI setting (effect not evaluated)")
    return findings


@dataclass
class OSInfo:
    name: str
    identifier: str
    version: str
    codename: str
    supported: bool
    message: str


@dataclass
class Package:
    name: str
    branch: str
    installed: str
    candidate: str | None = None


@dataclass
class Inventory:
    packages: dict
    held: set | None
    issues: list
    available: bool = True


@dataclass
class AuditReport:
    os_info: OSInfo | None = None
    inventory: Inventory | None = None
    directories: list = field(default_factory=list)
    records: list = field(default_factory=list)
    security_findings: list = field(default_factory=list)
    mitigations: list = field(default_factory=list)
    config_notes: list = field(default_factory=list)
    upgrade_notes: list = field(default_factory=list)
    services: dict = field(default_factory=dict)
    changes: list = field(default_factory=list)
    error: str | None = None


def detect_os(root, today=None):
    today = today or date.today()
    values = {}
    for path in [root / "etc/os-release", root / "usr/lib/os-release"]:
        if path.exists():
            resolved_path, resolved_root = path.resolve(), root.resolve()
            if resolved_path != resolved_root and resolved_root not in resolved_path.parents:
                # Never resolve an offline image's absolute link onto the host.
                continue
            for line in read_config(path).decode("utf-8").splitlines():
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                key, separator, value = line.partition("=")
                if separator and key in {"PRETTY_NAME", "ID", "VERSION_ID", "VERSION_CODENAME"}:
                    tokens = shlex.split(value, comments=False)
                    if len(tokens) > 1:
                        raise ValueError(f"Invalid os-release assignment: {path}: {key}")
                    values[key] = tokens[0] if tokens else ""
            break
    identifier = values.get("ID", "unknown")
    version = values.get("VERSION_ID", "unknown")
    codename = values.get("VERSION_CODENAME", "")
    if not codename:
        codename = {"12": "bookworm", "13": "trixie"}.get(version, "unknown")
    name = values.get("PRETTY_NAME", identifier)
    supported = False
    if identifier not in {"debian", "raspbian"}:
        message = "UNSUPPORTED BY THIS SCRIPT: OS identity/release coverage unknown; consult the OS/PHP vendor"
    elif (version.isdigit() and int(version) <= 11) or codename in {"bullseye", "buster", "stretch", "jessie", "wheezy"}:
        message = ("UNSUPPORTED RELEASE: outside ordinary Debian security/LTS coverage; "
                   "Bullseye LTS ended 2026-08-31. Migrate to a supported OS or verify separate vendor/ELTS coverage")
    elif codename not in RELEASES or RELEASES[codename][0] != version:
        message = "UNSUPPORTED BY THIS SCRIPT: unrecognized or inconsistent release; no verified distro upgrade guidance"
    elif today > RELEASES[codename][1]:
        message = f"UNSUPPORTED RELEASE: Debian baseline support ended {RELEASES[codename][1]}; review current vendor coverage"
    else:
        supported = True
        message = f"SUPPORTED RELEASE BASELINE: Debian {codename} through {RELEASES[codename][1]}; vendor/architecture coverage still applies"
    return OSInfo(name, identifier, version, codename, supported, message)


def query(arguments):
    """Only fixed read-only metadata commands call this helper."""
    try:
        result = subprocess.run(arguments, capture_output=True, text=True, timeout=10,
                                check=False, env={**os.environ, "LC_ALL": "C", "LANG": "C"})
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout if result.returncode == 0 else None


def version_fixed(branch, version):
    # Do not infer vendor backports, prereleases, or other PHP branches as fixed.
    match = re.fullmatch(r"(?:\d+:)?(\d+\.\d+\.\d+)(?:-[A-Za-z0-9.+~]+)?", version or "")
    return bool(match and branch in FIXED and match[1].rsplit(".", 1)[0] == branch
                and tuple(map(int, match[1].split("."))) >= tuple(map(int, FIXED[branch].split("."))))


def installed_fix_verified(inventory):
    return bool(inventory and inventory.available and inventory.packages
                and all(version_fixed(p.branch, p.installed) for p in inventory.packages.values()))


def php_support_finding(branch, today=None):
    today = today or date.today()
    if branch in PHP_EOL and today <= PHP_EOL[branch]:
        return None
    status = ("UPSTREAM UNSUPPORTED/EOL; vendor maintenance/backports need separate verification"
              if tuple(map(int, branch.split("."))) < (8, 2) or branch in PHP_EOL
              else "UNSUPPORTED BY THIS SCRIPT; upstream support and fix status unknown")
    return f"PHP {branch}: {status}"


def package_inventory(root):
    if root != Path("/") or sys.platform != "linux":
        return Inventory({}, None, ["Package inspection unavailable for an offline root or non-Linux host"], False)
    # Query all installed PHP source packages, including common/extensions/CLI,
    # so an upgrade command cannot silently omit another copy of PHP's core.
    text = query(["/usr/bin/dpkg-query", "-W", "-f=${binary:Package}\t${db:Status-Status}\t${Version}\t${source:Package}\n"])
    if text is None:
        return Inventory({}, None, ["Installed PHP package inventory unavailable; verify the serving PHP installation manually"], False)
    packages, issues = {}, []
    for line in text.splitlines():
        fields = line.split("\t")
        if len(fields) != 4 or fields[1] != "installed":
            continue
        name, _, installed, source = fields
        branch = re.fullmatch(r"php(\d+\.\d+)", source)
        if source == "php5":
            # Older Debian releases named the source and binary packages php5,
            # with the minor version visible only in the package version.
            branch = re.match(r"(?:\d+:)?(5\.\d+)\.", installed)
        if branch and re.fullmatch(PACKAGE_NAME, name):
            packages[name] = Package(name, branch[1], installed)
    if not packages:
        return Inventory({}, set(), [])
    held_text = query(["/usr/bin/apt-mark", *APT_CACHE_OPTIONS, "showhold"])
    held = set(held_text.split()) if held_text is not None else None
    if held is None:
        issues.append("APT hold state unavailable; upgrade commands withheld")
    policy = query(["/usr/bin/apt-cache", *APT_CACHE_OPTIONS, "policy", *sorted(packages)])
    if policy is None:
        issues.append("APT candidates unavailable; refresh indexes and check configured repositories")
    else:
        current = None
        for line in policy.splitlines():
            header = re.fullmatch(f"({PACKAGE_NAME}):", line)
            if header:
                current = header[1] if header[1] in packages else None
                if current is None and ":" not in header[1]:
                    # Some APT releases omit the native architecture in policy
                    # headers even when dpkg identifies a package with it.
                    matches = [name for name in packages if name.split(":", 1)[0] == header[1]]
                    current = matches[0] if len(matches) == 1 else None
            elif current and (candidate := re.fullmatch(r"\s+Candidate:\s+(\S+)\s*", line)):
                if candidate[1] != "(none)":
                    packages[current].candidate = candidate[1]
    return Inventory(packages, held, issues)


def held_package(name, held):
    return held is not None and (name in held or name.split(":", 1)[0] in held
                                or (":" not in name and any(item.split(":", 1)[0] == name for item in held)))


def upgrade_blockers(os_info, inventory, packages):
    blockers = []
    if not os_info.supported:
        blockers.append("OS support is not verified")
    if not all(version_fixed(p.branch, p.candidate) for p in packages):
        blockers.append("APT has no confirmed fixed package for this branch")
    if inventory.held is None:
        blockers.append("package holds could not be checked")
    elif any(held_package(p.name, inventory.held) and p.candidate != p.installed for p in packages):
        blockers.append("held packages would need to change")
    return blockers


def package_guidance(os_info, inventory, root, today=None):
    today = today or date.today()
    # Missing installed-package inventory blocks a conclusion. APT candidate or
    # hold-query issues only affect upgrade planning, not an installed fix.
    findings = [] if inventory.available else list(inventory.issues)
    print(f"OS {os_info.name}; ID={os_info.identifier}; version={os_info.version}; codename={os_info.codename}")
    print(os_info.message)
    if not os_info.supported:
        findings.append(os_info.message)
    print("APT candidates use existing LOCAL indexes; indexes were not refreshed and mirror/download availability is unverified.")
    if inventory.packages and not installed_fix_verified(inventory) and os_info.identifier in {"debian", "raspbian"}:
        print("Refresh indexes, then rerun this audit before using an upgrade plan:")
        print("  sudo apt update")
        print("apt update alone does not install updates or upgrade the OS to a new release.")
    if root != Path("/"):
        print("Offline audit: no upgrade commands can be derived from the host's packages.")
    for branch in sorted({p.branch for p in inventory.packages.values()}):
        packages = sorted((p for p in inventory.packages.values() if p.branch == branch), key=lambda p: p.name)
        if message := php_support_finding(branch, today):
            print(message)
            findings.append(message)
        print(f"PHP {branch}: upstream PHP2Shell fixed release {FIXED.get(branch, 'not recorded by this script')}")
        for package in packages:
            installed_fixed = version_fixed(branch, package.installed)
            candidate_fixed = version_fixed(branch, package.candidate)
            status = "FIXED INSTALLED" if installed_fixed else "INSTALLED FIX UNVERIFIED"
            candidate = "PATCHED CANDIDATE" if candidate_fixed else "NO VERIFIED PATCHED CANDIDATE"
            hold = "; HELD" if held_package(package.name, inventory.held) else ""
            print(f"PACKAGE {package.name}: installed={package.installed} [{status}]; candidate={package.candidate or 'none/unknown'} [{candidate}]{hold}")
            if not installed_fixed:
                findings.append(f"{package.name}: installed fix unverified; older versions may contain vendor backports")
        needs_upgrade = any(not version_fixed(branch, p.installed) for p in packages)
        blocked = [p.name for p in packages if held_package(p.name, inventory.held) and p.candidate != p.installed]
        if needs_upgrade and blocked:
            findings.append("HELD packages block an automatic upgrade recommendation: " + ", ".join(blocked))
        verified_candidates = all(version_fixed(branch, p.candidate) for p in packages)
        if needs_upgrade and not verified_candidates:
            findings.append(f"PHP {branch}: no complete verified patched candidate set; review repositories, pinning, mirror freshness, or vendor backports")
        names = [p.name for p in packages]
        print("Inspect package policy: " + shlex.join(["apt-cache", "policy", *names]))
        blockers = upgrade_blockers(os_info, inventory, packages)
        if needs_upgrade and not blockers:
            command = ["apt-get", "install", "--only-upgrade", "--no-remove", *names]
            print("After refreshing indexes and rerunning, simulate and review dependency changes:")
            print("  " + shlex.join(["apt-get", "--simulate", *command[1:]]))
            print("Upgrade these installed PHP packages (command shown only; not executed):")
            print("  " + shlex.join(["sudo", *command]))
        elif needs_upgrade:
            print("UPGRADE COMMAND WITHHELD: " + "; ".join(blockers))
        else:
            print(f"PHP {branch}: installed packages meet the upstream fix; running web workers remain unverified.")
    return findings


def restart_guidance(directories):
    services = sorted({"apache2.service" if d.name == "apache2" else f"php{d.parent.name}-fpm.service"
                       for d in directories if d.name != "cgi"})
    print("Worker services to review: " + (", ".join(services) or "CGI launcher/web server"))
    print("Full restart commands AFTER updates/configuration changes, for SAPIs actually serving the site (not executed):")
    for service in services:
        print("  " + shlex.join(["sudo", "systemctl", "restart", service]))
    if any(d.name == "cgi" for d in directories):
        print("CGI: persistent FastCGI launcher is unknown; identify its supervisor before restarting. One-shot CGI has no persistent PHP worker.")
    print("Configuration trees can be inactive. Confirm the serving SAPI; a graceful reload may retain old workers.")


def service_status(root, directories, inventory):
    """Read systemd state only; never start a service or make a web request."""
    units = {"apache2.service" if d.name == "apache2" else f"php{d.parent.name}-fpm.service"
             for d in directories if d.name != "cgi"}
    for package in inventory.packages.values():
        name = package.name.split(":", 1)[0]
        if name.startswith("libapache2-mod-php"):
            units.add("apache2.service")
        elif name == f"php{package.branch}-fpm":
            units.add(f"php{package.branch}-fpm.service")
    result = {}
    for unit in sorted(units):
        output = (query(["/usr/bin/systemctl", "show", "--property=LoadState,ActiveState", unit])
                  if root == Path("/") and sys.platform == "linux" else None)
        values = dict(line.split("=", 1) for line in (output or "").splitlines() if "=" in line)
        state = values.get("ActiveState", "unknown")
        result[unit] = "not installed" if values.get("LoadState") == "not-found" else state
    return result


def summary(report, args):
    """Keep the default console view short; detailed evidence is opt-in."""
    width = max(40, min(shutil.get_terminal_size((76, 24)).columns, 76))

    def line(message=""):
        print(textwrap.fill(message, width=width, subsequent_indent="  ",
                            break_long_words=False, break_on_hyphens=False) if message else "")

    def command(arguments):
        current = "  "
        for argument in map(shlex.quote, arguments):
            if len(current) > 2 and len(current) + len(argument) + 1 > width - 2:
                print(current.rstrip() + " \\")
                current = "    "
            current += argument + " "
        print(current.rstrip())

    inventory = report.inventory
    patched = installed_fix_verified(inventory)
    absent = bool(inventory and inventory.available and not inventory.packages and not report.directories)
    result = "CHECK FAILED" if report.error else "NOTHING TO CHECK" if absent else "CHECK COMPLETE"
    line(f"PHP2Shell: {result}")
    if not report.error and not absent:
        if report.security_findings:
            line("Result: security review needed.")
        else:
            line("Result: PHP security fix installed." if patched else "Result: no outstanding security findings.")
    writes = [path for kind, path in report.changes if kind == "write"]
    line(f"Settings files changed: {len(writes)}." if writes else "No settings changed.")
    if report.error:
        line("Error: " + report.error)
    if report.os_info:
        line()
        line("OS: " + report.os_info.name)
        if not report.os_info.supported:
            line(report.os_info.message)
    if inventory and not inventory.available:
        line("PHP packages: could not be checked.")
    if absent:
        line("No distribution PHP packages or web PHP configuration found.")
    branches = sorted(({p.branch for p in inventory.packages.values()} if inventory else set())
                      | {d.parent.name for d in report.directories})
    for branch in branches:
        packages = [p for p in inventory.packages.values() if p.branch == branch]
        if not packages:
            line()
            line(f"PHP {branch}: configuration found; package version not verified.")
            if issue := php_support_finding(branch):
                line(issue)
            continue
        installed = ", ".join(sorted({p.installed for p in packages}))
        candidates = ", ".join(sorted({p.candidate or "unknown" for p in packages}))
        fixed = all(version_fixed(branch, p.installed) for p in packages)
        ready = all(version_fixed(branch, p.candidate) for p in packages)
        line()
        line(f"PHP {branch}: {installed} ({len(packages)} packages)")
        line("Fix: installed package versions meet the upstream fix." if fixed else
             f"Fix: not verified. Upstream fixed release: {FIXED.get(branch, 'unknown for this branch')}.")
        line(f"APT candidate: {candidates}" + (" (patched)." if ready else
             " (upgrade availability unverified)." if fixed else " (fix not verified)."))
        if issue := php_support_finding(branch):
            line(issue)
        if not fixed and inventory.held is None:
            line("Package holds: could not be checked.")
        elif not fixed and any(held_package(p.name, inventory.held) for p in packages):
            line("Held PHP packages need review; see --verbose.")
    if report.services:
        line()
        labels = {"active": "running", "inactive": "not running", "failed": "failed",
                  "unknown": "status unavailable", "not installed": "not installed"}
        for unit, state in report.services.items():
            name = "Apache" if unit == "apache2.service" else unit[:-8]
            line(f"{name}: {labels.get(state, state)}")
        line("Service state only; PHP requests were not tested.")
    if report.records:
        line()
        line("Optional mitigations (not required for patched PHP):" if patched and not report.security_findings
             else "Optional partial mitigations (do not replace a PHP update):")
        missing = sum(not disabled(values.get("session.upload_progress.enabled")) for _, values in report.records)
        line(f"Upload progress: not disabled in {missing} web {'configuration' if missing == 1 else 'configurations'}." if missing else "Upload progress: disabled in checked web configurations.")
        if not args.allow_unlimited_memory and any(values.get("memory_limit") != "-1" for _, values in report.records):
            line("Unlimited-memory mitigation: not selected; current limit retained.")
        backtraces = sum(plan.directory.parent.name == "8.5" and not disabled(values.get("fatal_error_backtraces")) for plan, values in report.records)
        if backtraces:
            line(f"Fatal backtraces: not disabled in {backtraces} PHP 8.5 configuration(s).")
        limits = 0
        for _, values in report.records:
            post, upload = quantity(values.get("post_max_size")), quantity(values.get("upload_max_filesize"))
            limits += not post or post > 8 * MIB or not upload or upload >= post
        if limits:
            line(f"Request/upload limits: {limits} configuration(s) outside the mitigation profile.")
    if report.config_notes:
        count = len(report.config_notes)
        line(f"Configuration notes: {count} {'item' if count == 1 else 'items'} (informational); see --verbose.")
    if report.upgrade_notes:
        line(f"Upgrade metadata notes: {len(report.upgrade_notes)} (informational); see --verbose.")
    if report.security_findings:
        line(f"Security findings: {len(report.security_findings)}; see --verbose.")
    for kind, path in report.changes:
        line(f"{'Backup' if kind == 'backup' else 'Changed'}: {path}")
    restart_units = set()
    if inventory and branches and not report.error:
        needs = [branch for branch in branches if any(not version_fixed(branch, p.installed) for p in inventory.packages.values() if p.branch == branch)]
        if needs:
            line()
            line("Next: refresh APT indexes, then rerun this script.")
            command(["sudo", "apt", "update"])
            for branch in needs:
                packages = sorted((p for p in inventory.packages.values() if p.branch == branch), key=lambda p: p.name)
                blockers = upgrade_blockers(report.os_info, inventory, packages)
                if blockers:
                    line(f"PHP {branch}: " + "; ".join(blockers) + ".")
                else:
                    line(f"PHP {branch} upgrade plan (confirm again after refreshing):")
                    options = ["install", "--only-upgrade", "--no-remove", *[p.name for p in packages]]
                    command(["apt-get", "--simulate", *options])
                    command(["sudo", "apt-get", *options])
                    for unit, state in report.services.items():
                        if state == "active" and (unit == "apache2.service" or unit == f"php{branch}-fpm.service"):
                            restart_units.add(unit)
    if writes:
        for unit, state in report.services.items():
            if state == "active":
                restart_units.add(unit)
    if restart_units:
        line("Restart after upgrades/settings changes:")
        for unit in sorted(restart_units):
            command(["sudo", "systemctl", "restart", unit])
    line()
    if report.error:
        line("Inspection/apply did not finish. Any changed files listed above remain on disk.")
    elif absent:
        line("Custom/container PHP was not checked.")
    elif patched:
        line("Worker restart history and loaded PHP versions were not checked.")
    else:
        line("APT data may be stale. Settings are partial mitigations; a PHP update provides the complete fix.")
    line("Use --verbose for package, configuration and restart details.")


def validate_target(plan):
    # The write path must consist of trusted, real directories, never symlinks.
    inside_php = True
    for directory in [plan.target.parent, *plan.target.parent.parents]:
        info = directory.lstat()
        if not stat.S_ISDIR(info.st_mode):
            raise ValueError(f"Refusing a symlink/non-directory write path: {directory}")
        if inside_php and (info.st_uid != os.geteuid() or info.st_mode & 0o022):
            raise ValueError(f"Refusing an untrusted writable directory: {directory}")
        if directory == plan.directory.parents[1]:
            inside_php = False
    if plan.target.is_symlink():
        raise ValueError(f"Refusing a symlink at managed target: {plan.target}")
    if plan.target.exists():
        info = plan.target.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_uid != os.geteuid() or info.st_mode & 0o022:
            raise ValueError(f"Refusing an untrusted managed file: {plan.target}")
    current = read_config(plan.target) if plan.target.exists() else None
    if current != plan.previous:
        raise ValueError(f"Configuration changed during inspection; rerun: {plan.target}")


def apply_plans(plans, changes=None):
    # Preflight every destination before making the first change.
    for plan in plans:
        validate_target(plan)
    for plan in plans:
        if plan.previous == plan.contents:
            print(f"UNCHANGED {plan.target}")
            continue
        if plan.previous is not None:
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            backup = plan.target.with_name(f"{NAME}.bak-{stamp}-{uuid.uuid4().hex[:8]}")
            with backup.open("xb") as output:
                os.chmod(backup, 0o600)
                output.write(plan.previous)
            print(f"BACKUP {backup}")
            if changes is not None:
                changes.append(("backup", backup))
        descriptor, temporary = tempfile.mkstemp(prefix=f".{NAME}.", dir=plan.target.parent)
        try:
            with os.fdopen(descriptor, "wb") as output:
                output.write(plan.contents)
                output.flush()
                os.fsync(output.fileno())
                os.fchmod(output.fileno(), 0o644)
            os.replace(temporary, plan.target)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
        print(f"WROTE {plan.target}")
        if changes is not None:
            changes.append(("write", plan.target))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true", help="write managed INI snippets; does not restart services")
    parser.add_argument("--allow-unlimited-memory", action="store_true", help="include memory_limit=-1; removes PHP's per-request memory guard")
    parser.add_argument("--sapi", action="append", choices=SAPIS, help="limit to this web SAPI (repeatable; default: all discovered)")
    parser.add_argument("--root", type=Path, default=Path("/"), help="filesystem root for offline audit only")
    parser.add_argument("--web-root", type=Path, action="append", help="additional web root to inspect (repeatable)")
    parser.add_argument("--verbose", action="store_true", help="also show the full package and configuration evidence")
    args = parser.parse_args(argv)
    if args.apply and (args.root != Path("/") or sys.platform != "linux" or os.geteuid() != 0):
        parser.error("--apply requires Linux root on the live filesystem; offline roots are audit-only")
    report = AuditReport()
    details = io.StringIO()
    try:
        if not args.root.is_dir():
            raise ValueError(f"Audit root is not a directory: {args.root}")
        with redirect_stdout(details):
            print("PHP package status and optional partial mitigations.")
            report.os_info = detect_os(args.root)
            report.inventory = package_inventory(args.root)
            inventory = report.inventory
            report.security_findings = package_guidance(report.os_info, inventory, args.root)
            if inventory.available:
                report.upgrade_notes.extend(inventory.issues)
            directories = discover(args.root, args.sapi)
            report.directories = directories
            report.services = service_status(args.root, directories, inventory)
            if directories:
                restart_guidance(directories)
            plans = []
            for directory in directories:
                plan, values, scoped = inspect(directory, args.allow_unlimited_memory)
                plans.append(plan)
                report.records.append((plan, values))
                print(f"\nPHP {directory.parent.name}/{directory.name}: {directory}")
                branch, sapi = directory.parent.name, directory.name
                if not any(p.branch == branch for p in inventory.packages.values()):
                    if message := php_support_finding(branch):
                        print(message)
                        report.security_findings.append(message)
                expected = f"libapache2-mod-php{branch}" if sapi == "apache2" else f"php{branch}-{sapi}"
                if not any(p.name.split(":", 1)[0] == expected for p in inventory.packages.values()):
                    report.security_findings.append(f"{directory}: serving package {expected} not established; tree may be inactive/custom/offline")
                for key in sorted(KEYS):
                    print(f"  {key} = {values.get(key, 'not explicitly set')}")
                print("  Planned override (only with --apply): " + ", ".join(f"{key}={value}" for key, value in plan.settings.items()))
                report.config_notes.extend(scoped)
            if directories:
                web_roots = [args.root / "var/www", *(args.web_root or [])]
                report.config_notes.extend(override_findings(args.root, directories, web_roots))
            if args.apply:
                if any("memory_limit" in p.settings for p in plans):
                    print("MEMORY: unlimited PHP memory selected/preserved; review worker cgroup/container availability limits.")
                apply_plans(plans, report.changes)
            for index, plan in enumerate(plans):
                # Re-read after apply to catch later INI files that override it.
                _, values, _ = inspect(plan.directory, args.allow_unlimited_memory)
                report.records[index] = (plan, values)
                report.mitigations.extend(f"{plan.directory}: {item}" for item in settings_findings(values, plan.directory.parent.name))
            for finding in report.security_findings:
                print(f"SECURITY {finding}")
            for mitigation in report.mitigations:
                print(f"OPTIONAL MITIGATION {mitigation}")
            for note in report.config_notes:
                print(f"CONFIG NOTE {note}")
            for note in report.upgrade_notes:
                print(f"UPGRADE NOTE {note}")
    except (OSError, ValueError, UnicodeError) as error:
        report.error = str(error)
    summary(report, args)
    if args.verbose:
        print("\nDetailed evidence\n")
        print(details.getvalue(), end="")
    return 2 if report.error else 1 if report.security_findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
