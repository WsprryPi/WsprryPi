<!-- omit in toc -->
# Developer Notes

I use VS Code installed on my working laptop (Windows or Mac) and the [Visual Studio Code Remote—SSH](https://code.visualstudio.com/docs/remote/ssh) extension to access VS Code's feature set on a Raspberry Pi from a familiar Dev UI on my laptop.

<!-- omit in toc -->
## Table of Contents

- [Set up SSH to your PI](#set-up-ssh-to-your-pi)
- [Clone Repo](#clone-repo)
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

I use VS Code to develop this environment and connect my workstation to my Pi via the [VS Code Remote Development](https://code.visualstudio.com/docs/remote/remote-overview) option. This tool makes compiling and testing on the Pi very easy as I work from my laptop.

If you are going to use VS Code from your workstation:

1. In VS Code, install the `Remote Development` extension.

2. `View -> Command Palette -> >Remote-SSH:Connect Current Window to Host`

3. Select or enter your `{hostname}.local`

4. The local VS Code engine will install the VS Code Server on the Pi.

5. Use the "Open Folder" button and select the root of your repo on the Pi (e.g. `~/WsprryPi/`).

6. I use several VS Code extensions. You may note that VS Code will prompt you to install recommended extensions.  This is a configuration I added to the Git repo to make it easier.  You can choose not to use any or all of these extensions.

    You can paste them all in the terminal window at once.  It may seem to hang, even for minutes on a slower Pi, but it will work.

    ``` bash
    # Extensions installed on SSH: wsprrypi.local:
    # Generated with:
    # code --list-extensions | xargs -L 1 echo code --install-extension
    code --install-extension Extensions installed on SSH: wspr4:
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

7. Do great things. You are now using VS Code on your Pi; all compilation and execution happens there.

> [!IMPORTANT]
> Remember that the **Wsprry Pi** systemd daemon is running if you ran the installer. If you are executing from your dev environment, you may receive an error that says `wsprrypi` is already running. You can stop and deactivate these with:
>
> ``` bash
> sudo systemctl stop wsprrypi
> sudo systemctl disable wsprrypi
> ```

## Required Devel Libs

If you did not run `install.sh` from within the Wsprry Pi repo or with the WsprryPi curl command, will need some libs to execute the project:

- `git`
- `apache2`
- `php`
- `chrony`
- `libgpiod-dev` (`libgpiod2` or `libgpiod3` are required, but the installer or `libgpiod-dev` will pull the correct one in)

Install these (without running the installer) with:

``` bash
sudo apt install git apache2 php chrony libgpiod-dev -y
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

The installer blacklists the onboard snd_bcm2835 device, as Wsprry Pi uses this for generating the signal. You will need a reboot at some point before expecting Wsprry Pi to work correctly.
