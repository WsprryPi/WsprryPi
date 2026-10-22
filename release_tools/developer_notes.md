<!-- omit in toc -->
# Developer Notes

I use VS Code installed on my working laptop (Windows or Mac) and the [Visual Studio Code Remote—SSH](https://code.visualstudio.com/docs/remote/ssh) extension to access VS Code's feature set on a Raspberry Pi from a familiar Dev UI on my laptop.

<!-- omit in toc -->
## Table of Contents

- [Set up SSH to your PI](#set-up-ssh-to-your-pi)
- [Clone Repo](#clone-repo)
    - [Windows SSHFS Mount Option](#windows-sshfs-mount-option)
  - [64-Bit Raspberry Pi (`armhf`)](#64-bit-raspberry-pi-armhf)
  - [All OS](#all-os)
- [Local Validation](#local-validation)
- [Required Devel Libs](#required-devel-libs)
- [A Note About Submodules](#a-note-about-submodules)
- [Reboot](#reboot)

## Set up SSH to your PI

Any references to `{hostname}` should be replaced with the hostname of your target Pi.

1. If you are on Windows, have [Open SSH](https://windowsloop.com/install-openssh-server-windows-11/) installed.

2. Check that you have an SSH key generated on your system:

    - Linux or Mac (one line):

        ``` bash
        [ -d ~/.ssh ] && [ -f ~/.ssh/*.pub ] && echo "SSH keys already exists." || ssh-keygen
        ```

    - Windows PowerShell:

        ``` PowerShell
        if (Test-Path "$env:USERPROFILE\.ssh" -and (Test-Path "$env:USERPROFILE\.ssh\*.pub")) {
            Write-Host "SSH keys already exist."
        } else {
            ssh-keygen
        }
        ```

    - Windows Command Line:

        ``` cmd
        @echo off
        if exist "%USERPROFILE%\\.ssh" (
            if exist "%USERPROFILE%\.ssh\*.pub" (
            echo SSH keys already exist.
            ) else (
            ssh-keygen
            )
        ) else (
            ssh-keygen
        )
      ```

3. `ssh` to your `pi@{hostname}.local` with the target host password to ensure your `ssh` client and name resolution via zeroconf or mDNS. If you see:

    ``` text
    @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    @    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @
    @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    ```

    - Edit `~/.ssh/known_hosts` and remove any lines beginning with your target hostname
    - "Yes" to a prompt to continue connecting
    - Exit back out

4. Copy keys to host with (enter target host password when asked):

    ``` bash
    ssh-copy-id pi@{hostname}.local
    ```

5. Edit `~/.ssh/config` (or `$HOME\.ssh` on Windows) and add a stanza like this - be sure to mind the indentation:

    ``` bash
    Host {hostname}.local
        HostName {hostname}.local
        User pi
        Port 22
        PreferredAuthentications publickey
    ```

6. `ssh` to pi@{hostname}.local to ensure your changes allow key exchange (passwords) logins.

## Clone Repo

1. Once done and connected via SSH:

    > [!IMPORTANT]
    > You MUST clone the repo with `--recurse-submodules` to get all parts.

    Either:

    ``` bash
    sudo apt install git -y
    git clone --recurse-submodules -j8 https://github.com/WsprryPi/WsprryPi.git
    cd ~/WsprryPi/
    sudo ./scripts/install.sh -l
   ```

    (This will allow cloning git first, which you need anyway, then installing, which gets the rest of the libs.)

    Or:

    ``` bash
    curl -L installwspr.aa0nt.net | sudo bash
    git clone --recurse-submodules -j8 https://github.com/WsprryPi/WsprryPi.git
    cd ~/WsprryPi/
    ```

    (This lets the installer install everything, but then you clone the repo after.)

2. You should be in your git repo directory. Set up the Git global environment. Replace placeholders with your Git username and email:

    ``` bash
    git config --global user.email "you@example.com"
    git config --global user.name "Your Name"
    ```

## VS Code

I use VS Code to develop this environment and connect my workstation to my Pi via the [VS Code Remote Development](https://code.visualstudio.com/docs/remote/remote-overview) option when the target Pi supports it. This tool makes compiling and testing on the Pi very easy as I work from my laptop.

### 32-Bit Raspberry Pi (`armhf`)

For 32-bit/armhf Raspberry Pi work, Microsoft no longer provides VS Code support on armhf. The practical workflow for VS Code-like desktop tooling is to run VS Code on the workstation and edit the Pi-hosted checkout through an SSHFS-style mount.

Keep builds and compile-dependent validation on the Pi itself. VS Code or Codex access over a workstation-side mount is for editing and source inspection, not native validation, unless you explicitly intend otherwise.

#### macOS SSHFS Mount Option

On macOS, use macFUSE plus sshfs to mount the Pi checkout. A helper script for mounting a Pi repo using `~/.ssh/config` host aliases is available here:

<https://gist.github.com/lbussy/4e556402959ff6204144041c1ecb24cb>

Use the mounted checkout for editing and source inspection only. Run builds and validation on the Pi itself:

``` bash
ssh pi@{hostname}.local
cd ~/WsprryPi/src
make semantics-test
```

macFUSE/sshfs mounts can occasionally become stale. If that happens, force-unmount and re-mount the checkout before continuing.

#### Windows SSHFS Mount Option

For 32-bit/armhf Raspberry Pi work, Windows users can use [WinFsp](https://winfsp.dev/) plus [SSHFS-Win](https://github.com/winfsp/sshfs-win) to mount the Pi checkout as a Windows drive letter. This provides a workflow similar to macFUSE/sshfs on macOS when VS Code Remote SSH cannot install a supported VS Code Server on the target Pi.

Install WinFsp and SSHFS-Win from an elevated PowerShell prompt:

``` PowerShell
winget install WinFsp.WinFsp
winget install SSHFS-Win.SSHFS-Win
```

Map the Wsprry Pi checkout to a drive letter:

``` PowerShell
net use W: \\sshfs\pi@{hostname}.local\home\pi\WsprryPi
```

Open the mounted checkout in VS Code:

``` PowerShell
code W:\
```

Unmount the drive when finished:

``` PowerShell
net use W: /delete
```

As with macFUSE on macOS, use the mounted drive for editing and source inspection only. Run builds and validation on the Pi itself:

``` bash
ssh pi@{hostname}.local
cd ~/WsprryPi/src
make semantics-test
```

Windows drive-letter mounts can have additional friction with POSIX permissions, symlinks, filename case, and generated build artifacts, so avoid running compile-heavy workflows through the mounted drive.

### 64-Bit Raspberry Pi (`armhf`)

If you are going to use VS Code Remote SSH from your workstation on a supported target:

1. In VS Code, install the `Remote Development` extension.

2. `View -> Command Palette -> >Remote-SSH:Connect Current Window to Host`

3. Select or enter your `{hostname}.local`

4. The local VS Code engine will install the VS Code Server on the Pi.

### All OS

1. Use the "Open Folder" button and select the root of your repo on the Pi (e.g. `~/WsprryPi/`).

2. I use several VS Code extensions. You may note that VS Code will prompt you to install recommended extensions.  This is a configuration I added to the Git repo to make it easier.  You can choose not to use any or all of these extensions.

    You can paste them all in the terminal window at once.  It may seem to hang, even for minutes on a slower Pi, but it will work.

    ``` bash
    # Extensions installed on SSH: wsprrypi.local:
    # Generated with:
    # code --list-extensions | xargs -L 1 echo code --install-extension
    code --install-extension bmewburn.vscode-intelephense-client
    code --install-extension davidanson.vscode-markdownlint
    code --install-extension eamodio.gitlens
    code --install-extension ecmel.vscode-html-css
    code --install-extension foxundermoon.shell-format
    code --install-extension github.copilot
    code --install-extension github.copilot-chat
    code --install-extension github.vscode-pull-request-github
    code --install-extension gruntfuggly.todo-tree
    code --install-extension mechatroner.rainbow-csv
    code --install-extension mhutchie.git-graph
    code --install-extension ms-python.debugpy
    code --install-extension ms-python.python
    code --install-extension ms-python.vscode-pylance
    code --install-extension ms-python.vscode-python-envs
    code --install-extension ms-vscode.cmake-tools
    code --install-extension ms-vscode.cpptools
    code --install-extension njpwerner.autodocstring
    code --install-extension rifi2k.format-html-in-php
    code --install-extension streetsidesoftware.code-spell-checker
    code --install-extension timonwong.shellcheck
    code --install-extension yzhang.markdown-all-in-one
    code --install-extension xdebug.php-debug
    ```

> [!NOTE]
> If you are working on a mount on your local PC/Mac, remember to keep builds and compile-dependent validation on the Pi itself.  You will experience errors if you try to use VS Code on your workstation through a mount to compile.

3. Do great things. You are now using VS Code on your Pi; all compilation and execution happens there.

> [!IMPORTANT]
> Remember that the **Wsprry Pi** systemd daemon is running if you ran the installer. If you are executing from your dev environment, you may receive an error that says `wsprrypi` is already running. You can stop and deactivate these with:
>
> ``` bash
> sudo systemctl stop wsprrypi
> sudo systemctl disable wsprrypi
> ```

## Local Validation

The runtime semantics validation target includes both native regression tests and a Node-based log timestamp display regression. On a fresh Raspberry Pi or local validation environment, install Node.js before running the target or it may fail with `make: node: No such file or directory`.

```bash
sudo apt update
sudo apt install -y nodejs
```

Run local validation from the Pi checkout, not from the macFUSE mount:

```bash
cd ~/WsprryPi/src
make semantics-test
```

## Required Devel Libs

If you did not run `install.sh` from within the Wsprry Pi repo or with the WsprryPi curl command, will need some libs to execute the project:

- `git`
- `apache2`
- `php`
- `chrony`
- `nodejs` (required by local validation targets such as `make semantics-test`)
- `libgpiod-dev` (`libgpiod2` or `libgpiod3` are required, but the installer or `libgpiod-dev` will pull the correct one in)

Install these (without running the installer) with:

``` bash
sudo apt install git apache2 php chrony nodejs libgpiod-dev -y
```

## A Note About Submodules

I have opted to use submodules to reuse common elements in my projects, as well as to clearly delineate licensing of historic code. When you clone, use the `--recurse-submodules -j8` argument. Should you switch to a branch and find the submodules are no longer present, issue the following from the root of the repo:

``` bash
git submodule foreach --recursive 'git clean -xfd'
git submodule update --init --recursive
```

Or possibly better/cleaner:

```bash
git submodule update --init --recursive --force
```

## Reboot

For all Pi's before the Pi 5, the installer blacklists the onboard snd_bcm2835 device.  Wsprry Pi uses this for generating the signal on the GPIO so sound must be disabled to avoid conflicts. You will need a reboot at some point before expecting Wsprry Pi to work correctly.
armhf