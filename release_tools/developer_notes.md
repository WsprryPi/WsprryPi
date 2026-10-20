<!-- omit in toc -->
# Wsprry Pi Developer Guide

This guide serves two purposes:

- Walk a new contributor through preparing a Wsprry Pi development environment.
- Give experienced contributors a concise reference for common development tasks.

Wsprry Pi runs on Raspberry Pi hardware and controls GPIO and radio-transmission
functions. Source-level tests are not a substitute for installation, service,
GPIO, timing, or RF qualification on the intended hardware.

<!-- omit in toc -->
## Table of Contents

- [Development Model](#development-model)
- [New Developer Quick Start](#new-developer-quick-start)
- [Prepare the Raspberry Pi](#prepare-the-raspberry-pi)
- [Configure SSH From the Workstation](#configure-ssh-from-the-workstation)
  - [Create an SSH Key](#create-an-ssh-key)
  - [Verify the Host and Install the Key](#verify-the-host-and-install-the-key)
  - [Add an SSH Host Alias](#add-an-ssh-host-alias)
- [Clone the Development Checkout](#clone-the-development-checkout)
- [Verify the Repository and Components](#verify-the-repository-and-components)
- [Choose an Editing Workflow](#choose-an-editing-workflow)
  - [VS Code Remote SSH on a Supported 64-Bit Pi](#vs-code-remote-ssh-on-a-supported-64-bit-pi)
  - [macOS SSHFS for a 32-Bit Pi](#macos-sshfs-for-a-32-bit-pi)
  - [Windows SSHFS for a 32-Bit Pi](#windows-sshfs-for-a-32-bit-pi)
  - [Other Editors](#other-editors)
- [Install Development Dependencies](#install-development-dependencies)
  - [Full Wsprry Pi Installation](#full-wsprry-pi-installation)
  - [Packages for Source Work](#packages-for-source-work)
  - [Development and Diagnostic Tools](#development-and-diagnostic-tools)
  - [Install Codex CLI on a 64-Bit Pi](#install-codex-cli-on-a-64-bit-pi)
  - [Install Impeccable for WsprryPi-UI Work](#install-impeccable-for-wsprrypi-ui-work)
- [Repository Support for AI Agents](#repository-support-for-ai-agents)
- [Run Safe Source-Level Validation](#run-safe-source-level-validation)
- [Manage the Installed Service During Development](#manage-the-installed-service-during-development)
- [Git and Component Reference](#git-and-component-reference)
  - [Understand the Component Layout](#understand-the-component-layout)
  - [Inspect Component State](#inspect-component-state)
  - [Change a Component Intentionally](#change-a-component-intentionally)
- [Troubleshooting](#troubleshooting)
- [Reboot and Hardware Considerations](#reboot-and-hardware-considerations)
- [Experienced Developer Command Reference](#experienced-developer-command-reference)

## Development Model

The normal development model has two computers:

- **Workstation:** Your macOS, Windows, or Linux computer runs your editor and
  SSH client.
- **Raspberry Pi:** The Pi owns the checkout and runs Git, builds, validation,
  the Wsprry Pi program, and any authorized hardware-dependent work.

The active development branch is `devel`. One repository tracks the
first-party `WsprryPi-UI` component and the named reusable components under
`src/` as ordinary directories. A normal clone contains the complete source
tree. `docs/components/README.md` records the maintained component boundaries.

Use an SSHFS mount only for editing and source inspection. Run Git commands,
compilation, tests, service commands, and program execution in an SSH session
on the Pi. This avoids filesystem, permissions, symlink, filename-case, and
generated-artifact problems on workstation-side mounts.

## New Developer Quick Start

This is the shortest safe path to a development checkout. The later sections
explain each step and provide alternatives.

1. Install Raspberry Pi OS and complete its initial setup. Enable SSH, create
   the intended user, assign a hostname, and confirm that the Pi has network
   access.

2. From the **workstation**, verify that SSH works:

   ```bash
   ssh pi@{hostname}.local
   ```

   Replace `pi` if the Pi uses a different account. Replace `{hostname}` with
   the Pi hostname everywhere in this guide.

3. Configure public-key authentication as described in
   [Configure SSH From the Workstation](#configure-ssh-from-the-workstation).

4. On the **Raspberry Pi**, install Git and clone `devel`:

   ```bash
   sudo apt update
   sudo apt install -y git
   cd ~
   git clone --branch devel https://github.com/WsprryPi/WsprryPi.git
   cd ~/WsprryPi
   ```

5. Verify the checkout before making changes:

   ```bash
   git branch --show-current
   git status --short --branch
   test -f docs/components/README.md
   ```

   The branch must be `devel`. A clean new checkout should have no changed-file
   lines. The component-documentation check should return successfully.

6. Choose an editing workflow:

   - On a supported 64-bit (`arm64`/`aarch64`) Pi, VS Code Remote SSH is the
     simplest graphical workflow.
   - On a 32-bit (`armhf`) Pi, use a workstation-side SSHFS mount or a terminal
     editor.

7. Install the dependencies needed for your work. The full installer configures
   a working Wsprry Pi system and changes system state; the package-only path is
   better when you only need source development. See
   [Install Development Dependencies](#install-development-dependencies).

8. Run safe source-level validation on the **Pi**:

   ```bash
   cd ~/WsprryPi/src
   make semantics-test
   ```

   This does not transmit or qualify attached hardware. It does compile test
   binaries and create build artifacts inside the checkout.

## Prepare the Raspberry Pi

Before cloning the repository, confirm the following on the Pi:

- Raspberry Pi OS is installed and current enough for the target hardware.
- SSH is enabled.
- The development account can use `sudo` when installation or service work is
  intentionally performed.
- The hostname resolves from the workstation, normally as
  `{hostname}.local` through mDNS.
- The Pi has working package-repository and GitHub access.
- The system architecture is known:

  ```bash
  uname -m
  dpkg --print-architecture
  ```

Common results are `aarch64`/`arm64` for a 64-bit OS and `armv7l`/`armhf` for a
32-bit OS. The OS architecture, not merely the Pi model, determines whether a
remote editor server is supported.

## Configure SSH From the Workstation

Run the commands in this section on the **workstation**, not the Pi.

### Create an SSH Key

First check for an existing public key.

On macOS or Linux:

```bash
find ~/.ssh -maxdepth 1 -name '*.pub' -print -quit 2>/dev/null
```

On Windows PowerShell:

```powershell
Get-ChildItem "$env:USERPROFILE\.ssh\*.pub" -ErrorAction SilentlyContinue
```

If no public key is listed, create an Ed25519 key:

```bash
ssh-keygen -t ed25519
```

PowerShell uses the same `ssh-keygen -t ed25519` command when the Windows
OpenSSH client is installed. Protect the private key and never copy or share it.

### Verify the Host and Install the Key

Test password-based access before installing the public key:

```bash
ssh pi@{hostname}.local
```

If SSH reports that the remote host identification changed, first confirm that
the Pi was intentionally reinstalled, replaced, or assigned a previously used
hostname. An unexpected key change can indicate that you are connecting to the
wrong system. After confirming the change, remove only that saved host key:

```bash
ssh-keygen -R {hostname}.local
```

On macOS or Linux, install the public key with:

```bash
ssh-copy-id pi@{hostname}.local
```

On Windows, add the contents of the `.pub` file to `~/.ssh/authorized_keys` on
the Pi, or use this PowerShell command once:

```powershell
Get-Content "$env:USERPROFILE\.ssh\id_ed25519.pub" |
    ssh pi@{hostname}.local "umask 077; mkdir -p ~/.ssh; cat >> ~/.ssh/authorized_keys"
```

If the key has a different filename, substitute it in the command. Repeating
the PowerShell command may add a duplicate public-key line, which is harmless
but unnecessary.

Verify public-key authentication by opening a new connection:

```bash
ssh pi@{hostname}.local
```

The connection may ask for the key's passphrase, but it should no longer ask
for the Pi account password.

### Add an SSH Host Alias

An alias makes terminal, editor, and mount commands shorter. Edit
`~/.ssh/config` on macOS or Linux, or
`$env:USERPROFILE\.ssh\config` on Windows, and add:

```sshconfig
Host wsprrypi
    HostName {hostname}.local
    User pi
    Port 22
    PreferredAuthentications publickey
```

Use the actual Pi username if it is not `pi`. Test the alias:

```bash
ssh wsprrypi
```

The rest of this guide uses `wsprrypi` where an SSH alias is convenient.

## Clone the Development Checkout

Run these commands on the **Raspberry Pi**:

```bash
sudo apt update
sudo apt install -y git
cd ~
git clone --branch devel https://github.com/WsprryPi/WsprryPi.git
cd ~/WsprryPi
```

The command deliberately selects `devel`. The ordinary clone includes the UI
and all reusable component trees. If `~/WsprryPi` already exists, `git clone`
stops rather than overwriting it. Inspect the existing checkout instead of
deleting it.

Configure the Git identity used for future commits, replacing the examples:

```bash
git config --global user.name "Your Name"
git config --global user.email "you@example.com"
```

These settings identify commits; they do not authenticate a GitHub account.

## Verify the Repository and Components

From the **repository root on the Pi**:

```bash
cd ~/WsprryPi
git branch --show-current
git status --short --branch
test -f docs/components/README.md
for component in WsprryPi-UI src/Band-Lookup src/Chipset-Offsets \
    src/INI-Handler src/LCBLog src/Mailbox \
    src/MonitorFile src/PPM-Manager src/Signal-Handler src/Singleton \
    src/WSPR-Transmitter src/WSPR-Reference; do
    test -d "$component" || { printf 'Missing component: %s\n' "$component" >&2; exit 1; }
done
```

Confirm the following before editing or building:

- The branch is `devel`.
- Existing staged, unstaged, and untracked changes are understood and
  preserved.
- All component directories and the component-maintenance document are present.

Component files participate directly in the parent working tree and index, so
they require no separate repository initialization or state checks.

## Choose an Editing Workflow

VS Code is convenient but optional. Any editor that preserves Unix text files,
permissions, symlinks, and repository boundaries is suitable.

### VS Code Remote SSH on a Supported 64-Bit Pi

For a supported 64-bit (`arm64`/`aarch64`) target:

1. Install VS Code on the workstation.
2. Install the **Remote Development** extension.
3. Open the Command Palette and select **Remote-SSH: Connect Current Window to
   Host**.
4. Select `wsprrypi` or enter `pi@{hostname}.local`.
5. Open `/home/pi/WsprryPi` in the remote window.

VS Code installs its server on the Pi, so terminals, Git, compilation, and
validation in that remote window execute on the Pi.

The repository maintains optional editor recommendations in
`.vscode/extensions.json`. VS Code offers those recommendations when the
repository opens.

After installing the recommended extensions, run the following command in the
VS Code remote terminal to configure Todo Tree to use the installed `rg`
executable:

```sh
f="$HOME/.vscode-server/data/Machine/settings.json"
rg_path="$(command -v rg)" || { echo "ripgrep (rg) is not installed or not in PATH"; exit 1; }
mkdir -p "$(dirname "$f")"
F="$f" RG="$rg_path" python3 -c 'import json,os,pathlib; p=pathlib.Path(os.environ["F"]); d=json.loads(p.read_text()) if p.exists() else {}; d["todo-tree.ripgrep.ripgrep"]=os.environ["RG"]; p.write_text(json.dumps(d,indent=4)+"\n")'
```

Run this command on the Pi, then use **Developer: Reload Window** in VS Code.
If the remote settings file contains comments or trailing commas, edit the
Remote Settings JSON manually instead because the command expects strict JSON.

### macOS SSHFS for a 32-Bit Pi

For a 32-bit `armhf` Pi, install macFUSE and SSHFS on macOS, then mount the
Pi-hosted checkout for editing. A parameterized helper for mounts that use
`~/.ssh/config` aliases is available at:

<https://gist.github.com/lbussy/4e556402959ff6204144041c1ecb24cb>

For example, if that helper is installed as `mount-pi`:

```bash
mount-pi wsprrypi ~/WsprryPi /home/pi/WsprryPi
```

Use the mounted directory only for editing and source inspection. Use a second
terminal to run Git and validation on the Pi:

```bash
ssh wsprrypi
cd ~/WsprryPi
git status --short --branch
cd src
make semantics-test
```

Unmount with the helper documented by the mount tool. If a mount becomes stale,
unmount and recreate it before continuing; do not build through a stale mount.

### Windows SSHFS for a 32-Bit Pi

Windows developers can use [WinFsp](https://winfsp.dev/) and
[SSHFS-Win](https://github.com/winfsp/sshfs-win) to expose the Pi checkout as a
drive letter. Install them from an elevated PowerShell prompt:

```powershell
winget install WinFsp.WinFsp
winget install SSHFS-Win.SSHFS-Win
```

Map the checkout:

```powershell
net use W: \\sshfs\pi@{hostname}.local\home\pi\WsprryPi
```

Open it in VS Code if the `code` command is installed:

```powershell
code W:\
```

Unmount it when finished:

```powershell
net use W: /delete
```

Use the mapped drive only for editing and inspection. Run Git, builds, and
tests in a separate SSH session on the Pi. Windows mounts add potential POSIX
permissions, symlink, filename-case, and generated-artifact problems.

### Other Editors

Terminal editors on the Pi avoid mount and remote-server compatibility issues.
Graphical editors other than VS Code are also valid if they edit the Pi-hosted
files safely. Regardless of editor, run repository operations and compilation
on the Pi that owns the checkout.

## Install Development Dependencies

Choose the full system installation when preparing an operational Wsprry Pi
system, or package-only preparation for source development.

### Full Wsprry Pi Installation

> [!CAUTION]
> Run the local installer while the current working directory is inside the
> Wsprry Pi Git checkout. The installer uses the current working directory to
> decide whether the repository is a developer checkout that must be preserved.
> If it is invoked from outside the Git directory structure, even by using the
> script's absolute path, it treats `~/WsprryPi` as a temporary installation
> checkout and deletes that repository during cleanup.

Enter the existing checkout first, then invoke the installer with its relative
path:

```bash
cd ~/WsprryPi
sudo ./scripts/install.sh
```

Running the installer is an operational action, not merely dependency setup.
It installs packages, builds and installs Wsprry Pi, configures the web service
unless `--no-web` is selected, manages the system service, changes system
configuration, and may require a reboot. Review it and use it only when those
system changes are intended.

The convenience command `curl -L installwspr.aa0nt.net | sudo bash` is intended
for installing a Wsprry Pi system, not for creating a controlled `devel`
checkout. It executes downloaded code as root and follows the installer's
selected repository branch. Prefer an explicit `devel` clone for development.

### Packages for Source Work

The operational installer manages these runtime and installation packages:

- `git`
- `apache2`
- `php`
- `chrony`
- `libgpiod-dev`
- `libsystemd-dev`
- `libssl-dev`
- `age`

The encrypted support-bundle workflow requires the distribution-provided
`/usr/bin/age` and `/usr/bin/age-keygen` executables. The full installer installs
the `age` package and fails closed if those fixed executables are unavailable or
unsafe.

Source development additionally needs the compiler/build toolchain, Python,
Node.js, npm, PHP CLI, and—when running the browser integration suite—Chromium.
These development-only packages are intentionally separate from the operational
installer's package list. To prepare a Debian-based development host without
running the full installer:

```bash
sudo apt update && sudo apt install -y \
    git build-essential cmake pkg-config python3 \
    libgpiod-dev libsystemd-dev libssl-dev age \
    nodejs npm php-cli chromium
```

The parent `semantics-test` target uses Node.js for source regressions. UI
development uses the tracked private manifests at `WsprryPi-UI/package.json`
and `WsprryPi-UI/package-lock.json`. Install their exact development dependency
set and run the ordinary UI suite with:

```bash
cd ~/WsprryPi/WsprryPi-UI
npm ci --ignore-scripts
npm test
```

The pinned `ws` package is a test-only development dependency for mocked
WebSocket integration; it is not a Wsprry Pi production or browser-runtime
dependency. Chromium is required for the hardware-free browser integration
suite:

```bash
npm run test:browser
```

The active Debian CI workflow installs Chromium and runs both npm suites. Do
not commit `node_modules/`; it is generated locally and ignored.

### Development and Diagnostic Tools

Codex and other coding agents work more effectively when the development host
has fast search tools, language runtimes, and repository-specific validators.
The following packages are useful additions to a Wsprry Pi development system:

| Package | Purpose |
| --- | --- |
| `ripgrep` | Fast recursive source search through the `rg` command. |
| `fd-find` | Fast filename discovery through Debian's `fdfind` command. |
| `jq` | Inspection and transformation of JSON configuration and test output. |
| `shellcheck` | Static analysis of the repository's shell scripts. |
| `build-essential` | Standard compiler, linker, Make, and C/C++ build tools. |
| `pkg-config` | Discovery of installed compiler and linker dependencies. |
| `python3` | Execution of Python utilities and test helpers. |
| `python3-venv` | Isolated Python environments for optional tools. |
| `npm` | Installs the tracked WsprryPi-UI development and test dependencies. |
| `lsof` | Inspection of process-owned files and sockets during diagnostics. |
| `tmux` | Preserves long-running tests if an SSH connection drops. |
| `bubblewrap` | Sandboxed execution support used by development tooling. |

Install the optional toolkit on the **development host** with:

```bash
sudo apt update
sudo apt install -y \
    ripgrep fd-find jq shellcheck build-essential pkg-config \
    python3 python3-venv npm lsof tmux bubblewrap
```

Node.js and npm are already included in the source-development packages above.
On Debian-based systems, the executable installed by `fd-find` is named
`fdfind`.

This toolkit prepares the host for efficient repository work. Install and
configure the selected AI agent separately by following its current platform,
architecture, authentication, and update instructions. For Codex, use the
[current Codex documentation](https://developers.openai.com/codex/).

### Install Codex CLI on a 64-Bit Pi

This optional setup is for a developer who wants to use Codex to assist with
Wsprry Pi development. Codex is not required to build, test, install, or run
Wsprry Pi.

First confirm that the Pi is running a 64-bit operating system:

```bash
uname -m
```

Continue when the result is `aarch64` or `arm64`. A result such as `armv7l`
indicates a 32-bit operating system and is not compatible with the Linux ARM64
Codex CLI binary.

OpenAI's standalone installer is the preferred installation method on Linux:

```bash
curl -fsSL https://chatgpt.com/codex/install.sh | sh
```

The installer normally places the `codex` command in `~/.local/bin`. Add that
directory to Bash's persistent `PATH`, reload the shell configuration, and
verify the installation:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
source "$HOME/.bashrc"
hash -r
codex --version
```

Before appending the line, check whether `~/.bashrc` already adds
`~/.local/bin` to `PATH` to avoid duplicate entries. If the installer completed
but `codex` is still not found, test the installed file directly:

```bash
ls -l "$HOME/.local/bin/codex"
"$HOME/.local/bin/codex" --version
```

The official npm package is an alternative when Node.js and npm are available:

```bash
npm install -g @openai/codex
codex --version
```

Do not use `sudo npm install -g` merely to work around npm permissions. Use a
user-owned npm prefix instead, then ensure the same `~/.local/bin` directory is
on `PATH`:

```bash
npm config set prefix "$HOME/.local"
npm install -g @openai/codex
export PATH="$HOME/.local/bin:$PATH"
codex --version
```

Persist the `PATH` setting in `~/.bashrc` as shown above. Run `codex` from the
Wsprry Pi checkout, sign in using one of the offered authentication methods,
and ask it to read `AGENTS.md` before beginning development work. Re-running
the standalone installer is also the documented way to update Codex. Consult
the [current Codex CLI documentation](https://developers.openai.com/codex/cli)
if its platform requirements or installation commands have changed.

### Install Impeccable for WsprryPi-UI Work

[Impeccable](https://impeccable.style/tutorials/getting-started/) is an
optional Codex skill used only for work affecting the first-party
`WsprryPi-UI` component. It is not required for ordinary parent-repository C++,
scheduling, radio, installer, maintenance-script, or unrelated documentation
work.

The repository's `AGENTS.md` requires Impeccable for UI work, including
UI-owned PHP, JavaScript, CSS, templates, controls, wording, and interaction
behavior. A transport-only endpoint such as
`WsprryPi-UI/data/log_stream.php` still triggers this rule, although visual
rendering may not apply.

Install Impeccable globally for the Pi user so Codex can discover it without
adding skill or runtime files to the repository. On the **Raspberry Pi**, first
check Node.js and `npx`:

```bash
node --version
npx --version
```

The current Impeccable installer requires Node.js 22.12 or newer. Consult the
[current Impeccable installation documentation](https://impeccable.style/tutorials/getting-started/)
if that requirement or the installation command changes. Install the Codex
skill globally for the Pi user with:

```bash
npx --yes impeccable@latest skills install \
    -y \
    --providers=codex \
    --scope=global
```

`npx` runs the installer, so a separate global `npm install impeccable` is not
required. Completely restart Codex after installation, then verify that the
new session lists or recognizes `$impeccable`.

Global installation controls skill availability, not usage scope. Codex should
invoke Impeccable only when the authorized task affects `WsprryPi-UI`. Do not
commit `.agents/`, `.impeccable/`, `.claude/`, `.codex/`, `skills-lock.json`,
or local Node package artifacts unless they are separately approved as
intended repository content.

## Repository Support for AI Agents

The following tracked files help Codex or other development assistants work in
this repository:

- `AGENTS.md` is the authoritative repository-wide instruction file for AI
  coding agents. It defines scope control, dirty-worktree preservation,
  Raspberry Pi and RF safety, component boundaries, validation expectations,
  and documentation responsibilities. A more deeply nested `AGENTS.md` would
  take precedence within its directory tree.
- `.vscode/extensions.json` contains optional VS Code extension
  recommendations, including GitHub Copilot Chat and developer validation
  tools. `AGENTS.md` remains the authoritative repository instruction source.
- `.gitignore` excludes `.codex/`, `.agents/`, `skills-lock.json`, and root-level
  Node package metadata used by agent or tool sessions. The private
  `WsprryPi-UI/package.json` and lockfile are intentional tracked development
  manifests; `WsprryPi-UI/node_modules/` remains ignored generated content.
- `src/WSPR-Reference/.gitignore` separately excludes `.codex/` state within
  that component directory.

`AGENTS.md` also directs contributors to keep local `.agents/`, `.impeccable/`,
`.claude/`, and `.codex/` runtime artifacts out of commits unless they are
explicitly approved as intended repository content.

An AI agent should begin by reading `AGENTS.md`, inspecting the complete parent
working tree and affected component paths, and confirming the authorized scope.
Component changes are reviewed and committed through the parent repository.
The separate operator-documentation repository remains its own instruction,
change, validation, commit, and push boundary.

## Run Safe Source-Level Validation

Run the current aggregate semantics target on the **Raspberry Pi**:

```bash
cd ~/WsprryPi/src
make semantics-test
```

This target builds and runs native runtime-semantics and UI/source regression
binaries, then runs Node-based log-timestamp and update-comparison regressions.
It creates build artifacts in the checkout. It does not intentionally start a
transmission, key transmitter GPIO, install a binary, manage a service, or
reboot the Pi.

Reserve targets such as `test-tone`, `test-oneshot`, GPIO qualification, and
live-monitor targets for an explicit hardware test plan and authorization.
Those targets may use `sudo`, GPIO, a transmitter, or RF-producing paths.

Passing source-level tests does not qualify:

- Installation or upgrade behavior
- The systemd service lifecycle
- GPIO selection or electrical behavior
- Frequency accuracy or RF output
- Timing on the intended Pi model and OS
- Attached transmitters, filters, antennas, or loads

## Manage the Installed Service During Development

A full installation normally runs Wsprry Pi through the `wsprrypi` systemd
service. A second developer-started instance may then report that Wsprry Pi is
already running.

Inspect the service before changing it:

```bash
systemctl status wsprrypi
systemctl is-enabled wsprrypi
```

For a development session, stopping the service ends the current service
process but does not alter boot behavior:

```bash
sudo systemctl stop wsprrypi
```

Disabling it is a separate, persistent choice that prevents automatic startup
at boot:

```bash
sudo systemctl disable wsprrypi
```

Do not disable the service unless that persistent change is intended. Restore
normal startup and start the service with:

```bash
sudo systemctl enable wsprrypi
sudo systemctl start wsprrypi
```

Service commands change the operating system and can interrupt active work.
Confirm that no transmission or other required operation is in progress first.

## Git and Component Reference

### Understand the Component Layout

Wsprry Pi tracks the application and all twelve components in one repository:

- `WsprryPi-UI` is the editable first-party web interface.
- `src/Band-Lookup`, `src/Chipset-Offsets`, `src/INI-Handler`, `src/LCBLog`,
  `src/Mailbox`, `src/MonitorFile`,
  `src/PPM-Manager`, `src/Signal-Handler`, `src/Singleton`,
  `src/WSPR-Transmitter`, and `src/WSPR-Reference` are coherent reusable or
  supporting components.

Component files are ordinary parent-repository content. Preserve each named
root, public interface, standalone build/test entry points, attribution, and
extraction potential. `LCBLog` and `WSPR-Reference` in particular must remain
independently understandable and free of new WsprryPi-internal dependencies.

### Inspect Component State

The parent status covers every component:

```bash
cd ~/WsprryPi
git status --short --branch
git diff --check
git diff --cached --check
```

Use path-limited commands when reviewing a component portion of the parent
diff:

```bash
git status --short -- src/LCBLog
git diff -- src/LCBLog
git diff --cached -- src/LCBLog
```

Use `docs/components/README.md` for the component inventory, maintenance rules,
licensing, and extraction guidance. Do not initialize nested Git repositories
inside ordinary component directories.

### Change a Component Intentionally

When an approved change belongs in a component:

1. Inspect the complete parent status and the affected component path.
2. Preserve existing staged, unstaged, and untracked work.
3. Keep the change within the component's public and reuse boundaries.
4. Run its safe standalone tests where available.
5. Run applicable parent integration tests.
6. Review the component portion and complete staged parent diff.
7. Commit and push through only the authorized parent branch and remote.

Future extraction or publication requires a separate reviewed workflow and
authorization.

## Troubleshooting

### The Pi hostname does not resolve

Try its IP address to separate SSH from mDNS problems:

```bash
ssh pi@{ip-address}
```

Check the Pi hostname with `hostname` in a local Pi terminal. Confirm both
systems are on reachable networks and that client isolation is not enabled.

### SSH says the host identification changed

Confirm that the Pi was intentionally reinstalled, replaced, or renamed. Then
remove only the obsolete entry and reconnect:

```bash
ssh-keygen -R {hostname}.local
ssh pi@{hostname}.local
```

### A component directory is missing

An ordinary clone should contain all twelve component directories. First preserve
and inspect the current parent state:

```bash
cd ~/WsprryPi
git status --short --branch
git log -5 --oneline
git ls-tree HEAD -- WsprryPi-UI src
```

Do not copy a replacement from an installed system or another checkout,
and do not clean or reset a checkout that contains unexplained work. If the
directory is absent from `HEAD`, confirm that the intended branch and commit are
checked out. If it is tracked at `HEAD` but absent from disk, stop and determine
why before choosing a recovery operation.

### A component contains unexpected changes

Inspect the component through the parent repository before acting:

```bash
git status --short -- {component-path}
git diff -- {component-path}
git diff --cached -- {component-path}
```

The changes may be intentional user work. Do not reset, clean, stash, overwrite,
or discard them without explicit authorization.

### An SSHFS mount is stale

Stop editing, unmount it with the tool that created it, and mount it again.
Confirm the real checkout through SSH before resuming. Do not clean or rebuild
the repository through a stale mount.

### `make semantics-test` cannot find Node

Install Node.js on the Pi and retry:

```bash
sudo apt update
sudo apt install -y nodejs
cd ~/WsprryPi/src
make semantics-test
```

### UI tests cannot find `ws` or Chromium

Install the exact tracked UI development dependencies:

```bash
cd ~/WsprryPi/WsprryPi-UI
npm ci --ignore-scripts
npm test
```

If `npm run test:browser` reports that Chromium is missing, install the
development-host Chromium package and retry. The `ws` package remains test-only
and should come from the tracked lockfile rather than a global installation.

### Wsprry Pi is already running

Inspect the installed service before starting a development instance:

```bash
systemctl status wsprrypi
```

If it is safe and intentional to interrupt it, stop it for the development
session with `sudo systemctl stop wsprrypi`. Disabling boot-time startup is
usually unnecessary.

## Reboot and Hardware Considerations

The installer can blacklist the onboard `snd_bcm2835` module so Wsprry Pi can
use the relevant Raspberry Pi peripheral without an audio-driver conflict. A
reboot is required when that system configuration changes before the new
module state takes effect. Follow the installer's final message.

A reboot is not required after every source edit, compilation, or
`semantics-test` run. Reboot only for a known system-level change and only when
interrupting the Pi is safe.

Basic environment preparation does not authorize live hardware or RF testing.
Before any such test, establish the exact Pi, GPIO backend and pin, frequency,
mode, duration, attached transmitter and load, stopping procedure, and local
regulatory constraints.

## Experienced Developer Command Reference

Run these commands on the **Pi that owns the checkout**.

Inspect the parent checkout:

```bash
cd ~/WsprryPi
git branch --show-current
git status --short --branch
```

Confirm the component inventory:

```bash
test -f docs/components/README.md
for component in WsprryPi-UI src/Band-Lookup src/Chipset-Offsets \
    src/INI-Handler src/LCBLog src/Mailbox \
    src/MonitorFile src/PPM-Manager src/Signal-Handler src/Singleton \
    src/WSPR-Transmitter src/WSPR-Reference; do
    test -d "$component" || { printf 'Missing component: %s\n' "$component" >&2; exit 1; }
done
```

Run aggregate source-level validation:

```bash
cd ~/WsprryPi/src
make semantics-test
```

Inspect the installed service without changing it:

```bash
systemctl status wsprrypi
systemctl is-enabled wsprrypi
```

Review all parent and component changes:

```bash
cd ~/WsprryPi
git diff --check
git diff
git diff --cached --check
git diff --cached
```

Before reporting work complete, state separately what was source-validated and
what still requires installation, service, GPIO, hardware, timing, or RF
qualification.
