# Wsprry Pi Scripts

These scripts are used for install and development orchestration.

## PHP2Shell configuration audit and partial mitigations

`php2shell-mitigate.py` checks Debian/Raspberry Pi OS web PHP configurations under `/etc/php/<version>/{apache2,fpm,cgi}` for the partial mitigations in the [PHP security advisory of 21 October 2026](https://www.php.net/archive/2026.php#2026-10-21-1). It requires Python 3.8 or later and is separate from the installer. It does not run PHP, send web requests, update packages, restart services, or operate WsprryPi or transmitter hardware.

**Install the PHP security updates from your distribution. Configuration changes alone do not fix PHP2Shell.** The upstream fixes are PHP 8.2.35, 8.3.36, 8.4.27, and 8.5.12. Distribution backports can retain an older upstream version; the script reports these as requiring vendor verification for both CVE-2026-109618 and CVE-2026-109619 instead of declaring them vulnerable or fixed. Package metadata does not establish which PHP binary the web server uses or whether running workers have loaded an update.

The script detects OS identity, version and codename from `/etc/os-release`, falling back to `/usr/lib/os-release` when needed, without executing either file. Its release policy was reviewed on 21 October 2026: Debian/Raspbian Bookworm and Trixie use the Debian support baseline through 30 June 2028 and 30 June 2030 respectively. [Bookworm support](https://www.debian.org/releases/bookworm/) and [Trixie support](https://www.debian.org/releases/trixie/) remain subject to vendor and architecture coverage. Bullseye and earlier are clearly labeled **UNSUPPORTED RELEASE** under ordinary Debian security/LTS coverage; Bullseye LTS ended 31 August 2026, and separately maintained [ELTS packages](https://www.debian.org/releases/bullseye/) require vendor verification. Unknown distributions, future releases, or conflicting version/codename information are labeled **UNSUPPORTED BY THIS SCRIPT**, which does not claim that the vendor has ended support. PHP branches outside their recorded upstream support dates are separately flagged; vendor support may differ. These warnings do not prevent explicitly requested partial INI mitigations on a compatible Linux configuration tree.

On a live Linux host, read-only `dpkg-query`, `apt-mark showhold`, and `apt-cache policy` queries identify installed PHP source packages, their full package versions, holds, and APT candidates for the host's configured repositories and architecture. Common, CLI, and extension packages built from the same PHP source are included, alongside Apache/FPM/CGI packages. Candidate versions are compared with the upstream fixed release for their own branch; prereleases, unrecognized branches and older possible vendor backports remain unverified. The script disables APT's persistent package-cache output and never runs `apt update`, downloads packages, or contacts a package repository. Candidates reflect existing local indexes, so stale indexes, missing security sources, pinning and mirror lag must still be considered.

When an installed PHP fix is unverified, the report prints `sudo apt update` with an instruction to rerun the audit afterward. For a recognized supported release, when an update is needed and every installed package in that PHP source branch has a verified patched candidate with known, non-blocking hold state, it prints the exact installed package names in both a dependency simulation and a targeted upgrade command. For example:

```sh
apt-get --simulate install --only-upgrade --no-remove libapache2-mod-php8.2 php8.2-cli php8.2-common
sudo apt-get install --only-upgrade --no-remove libapache2-mod-php8.2 php8.2-cli php8.2-common
```

The actual command lists all detected packages from that PHP source branch. Review the simulation before upgrading; dependency resolution, downloaded package availability, and unchanged APT policy are not established by a cached candidate check. The commands use the candidates current when run instead of pinning a potentially stale version. Unsupported/unrecognized releases, unverified candidate sets, or blocking/unknown holds suppress the upgrade command while the installed fix remains unverified. Candidate and hold metadata affect upgrade planning; they do not invalidate an already verified installed fix. The script never overrides a hold or proposes an automatic major OS/PHP branch migration. It still prints package-policy inspection commands, and all commands in the report are instructions only, including when `--apply` is selected.

Audit without making changes, from the repository root:

```sh
python3 scripts/php2shell-mitigate.py
```

The default report groups packages by PHP branch and puts the result, installed/candidate versions, web-service state, settings that need attention, and next action together. A completed audit that needs review is distinct from an inspection failure. If package inventory succeeds and finds no distribution PHP packages or web configuration, the report says **NOTHING TO CHECK**. Custom/container PHP remains outside that conclusion. Full package rows, configuration locations and conditional restart instructions are available with:

```sh
python3 scripts/php2shell-mitigate.py --verbose
```

When every detected PHP package meets its branch's fixed release and there are no unresolved support or web-package coverage findings, the result is **PHP security fix installed**, with exit status `0`. Partial mitigation settings, including unlimited memory, are not required for patched PHP. The report lists their current state separately under **Optional mitigations**. Generic server overrides and scoped/per-directory INI settings are informational configuration notes: finding a directive does not establish that it is dangerous. Verbose evidence distinguishes `SECURITY`, `OPTIONAL MITIGATION`, `CONFIG NOTE` and `UPGRADE NOTE` entries. Unsupported releases, unverified installed versions and web configuration trees without their expected installed package still require security review. A successful package check does not verify loaded worker versions, restart history or live PHP requests.

On a live Linux system the script uses read-only `systemctl show` queries for the relevant Apache/FPM services. It reports running, stopped, failed or unavailable status without starting anything. Installed PHP packages and configuration trees do not prove PHP requests are being served; no PHP execution or web request is attempted. Summary restart commands apply only to services reported active and are shown after an available upgrade plan or changed settings. Verbose output retains conditional commands for all discovered web PHP trees.

Apply the ordinary partial mitigations on the Pi:

```sh
sudo python3 scripts/php2shell-mitigate.py --apply
```

This creates `/etc/php/<version>/<sapi>/conf.d/99-wsprrypi-php2shell.ini` for every discovered web SAPI. It disables session upload progress, caps `post_max_size` at 8 MiB, keeps `upload_max_filesize` below that limit, and disables `fatal_error_backtraces` on PHP 8.5. Smaller positive request limits are preserved. Body/upload limits must use canonical decimal bytes or K/M/G suffixes; inspection refuses leading-zero/octal quantities, environment substitutions and expressions it cannot safely preserve. Other PHP settings, including shell functions used by the existing log viewer, remain unchanged. The 8 MiB profile is intended for WsprryPi; review compatibility before using it on a shared server with applications that accept larger uploads. A CGI configuration tree must have the same Debian `php.ini` and `conf.d` layout; custom launchers need separate review.

For PHP that cannot yet be updated, PHP also recommends `memory_limit=-1` as a temporary way to avoid the PHP memory-limit failure path. Patched PHP does not need this mitigation. Removing that guard can exhaust Pi memory. This change requires a separate explicit option:

```sh
sudo python3 scripts/php2shell-mitigate.py --apply --allow-unlimited-memory
```

Use this option only with an appropriate worker/container/cgroup availability boundary. OS allocation failure can still reach the vulnerable code. `RLIMIT_AS` and `RLIMIT_DATA` are unsuitable substitutes. INI settings can be overridden by applications, pools, or directory configuration; review `php_admin_value` settings with the server administrator when locking `memory_limit` is needed. The script inventories possible overrides rather than changing them automatically. A later apply without the memory option preserves a previous explicit opt-in in the managed file.

`--sapi apache2`, `--sapi fpm`, or `--sapi cgi` limits the selection; repeat the option to select several. Without it, installed configuration trees are audited even if they are inactive. `--root /absolute/offline/root` supports read-only fixture or image audits; apply mode requires root on a live Linux filesystem. `/var/www` is inspected for `.htaccess` and `.user.ini`; repeat `--web-root /absolute/custom/document/root` for additional web roots. Apache/nginx configuration and selected FPM pool directories are inventoried for PHP overrides and possible pre-request diagnostic sources, including settings in inactive files. Findings show file and line locations without exposing their values. Fix invalid/deprecated per-request settings after reviewing the [stale-state advisory](https://github.com/php/php-src/security/advisories/GHSA-6wmf-4rv5-p3rp); hiding diagnostics does not mitigate that path.

The audit reads `php.ini` and alphabetically ordered `conf.d/*.ini`, reports relevant scoped INI settings, and checks again after applying. Server scanning covers `.conf`/`.load` files, standard FastCGI/SCGI/uWSGI parameter files, and extensionless site files in `sites-available`/`sites-enabled`; unrelated keys, certificates and binary files are not opened. It does not fully resolve web-server includes or custom configuration filenames, alternate `PHP_INI_SCAN_DIR`/`PHPRC` paths, custom per-directory INI filenames, application `ini_set()` calls, or active virtual hosts. It reports directory symlinks it does not traverse. Inspection fails on unreadable, non-regular, non-UTF-8, or larger-than-2-MiB selected configuration files rather than silently skipping them. Validate the actual serving SAPI and effective request settings separately. No public `phpinfo()` or diagnostic endpoint is created.

Apply mode refuses foreign files at its managed filename, symlinked write paths, hardlinked managed files, and untrusted directory/file ownership or write permissions. Every destination is preflighted before writing. Replacing an owned snippet makes an exact, mode-0600 `.bak-<UTC timestamp>-<unique suffix>` copy beside it; unchanged files are not rewritten. Writes are atomic per file, not transactional across all PHP trees. If a later write fails, the output identifies the files already written. Existing `php.ini`, package snippets, Apache sites, FPM pools, and application files are preserved.

After applying, review the reported conflicts, install vendor updates, and arrange a **full restart of every serving PHP worker**. For discovered Apache module trees the verbose report prints `sudo systemctl restart apache2.service` once; for each FPM tree it prints the versioned command, such as `sudo systemctl restart php8.2-fpm.service`. These commands are conditional on that SAPI actually serving the site: installed configuration trees can be inactive, and the script never operates services. For CGI it reports that a persistent FastCGI supervisor must be identified separately; one-shot CGI has no persistent PHP worker. A graceful reload can leave old workers alive. Confirm the actual web SAPI, patch level, effective settings, and WsprryPi web/log workflow after the restart. The request-size settings do not configure web-server/proxy decoded-body limits, field/part/header/nesting limits, cgroups, or application error handling; review those separately using the [OOM advisory](https://github.com/php/php-src/security/advisories/GHSA-w8hw-w9fx-j79w).

To undo a first apply, remove only the exact managed snippet paths reported by the script. To undo a replacement, restore the corresponding reported backup to its original snippet path with root ownership and mode 0644. Review the restored settings, arrange the same worker restart, and audit again. Removing a snippet restores the underlying configuration, which may be less secure; keep vendor PHP updates installed.

Exit status `0` means no unresolved security or package-coverage findings were identified (including a supported system with no distribution PHP packages or web configuration), `1` means the check completed with security/support/coverage review remaining, and `2` means inspection or application failed. Unsupported OS/PHP releases, unavailable installed-package inventory, unverified installed fixes and web trees without an expected installed package produce status `1`. Optional partial-mitigation gaps, generic configuration notes, and APT candidate/hold metadata issues do not independently change the exit status. Applying mitigations cannot clear an unverified installed fix. Inspection and write failures remain errors even when packages are patched. Even status `0` does not establish runtime protection; arrange worker restarts after package/configuration changes and verify the serving PHP installation separately.

Run the offline fixture tests from `src`:

```sh
make php2shell-test SUDO=
```

When ShellCheck is installed, the suite also checks the generated shell commands without executing them. Otherwise that check is reported as skipped; the remaining Python tests still run.
