# SPDX-License-Identifier: MIT
"""Observe RP1 drive choices in the selected WsprryPi source."""

import ast
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess
import tempfile

from rp1_gpclk_dkms_install import ContractError, SHA40, require, strict_json


SELECTOR_ID = "rp1_gpio_drive_ma"
UI_VIEW = "WsprryPi-UI/data/views/config.php"
UI_SCRIPT = "WsprryPi-UI/data/index.js"
CLI_OPTIONS = "src/rp1_gpio_drive_options.hpp"
CLI_PARSER = "src/arg_parser.cpp"
INSTALLER = "scripts/rp1_gpclk_dkms_install.py"
REQUIREMENTS = "config/rp1-provider-requirements.json"
PROBE_ENV = {"PATH": "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin", "LC_ALL": "C"}


def git(source, *args):
    return subprocess.check_output(
        ["git", "--no-replace-objects", "--no-optional-locks", "-c",
         "core.fsmonitor=false", "-C", str(source), *args],
        env={"PATH": "/usr/bin:/bin", "LC_ALL": "C", "GIT_CONFIG_NOSYSTEM": "1",
             "GIT_CONFIG_GLOBAL": "/dev/null"},
        timeout=30, text=True,
    )


def consumer_identity(source):
    require(source.is_absolute() and source.resolve(strict=True) == source and source.is_dir(),
            "canonical WsprryPi consumer source required")
    require(Path(git(source, "rev-parse", "--show-toplevel").strip()).resolve() == source,
            "WsprryPi consumer source is not a repository root")
    origin = git(source, "remote", "get-url", "origin").strip()
    require(re.fullmatch(r"(?:https://github\.com/|ssh://git@github\.com/|git@github\.com:)"
                         r"WsprryPi/WsprryPi(?:\.git)?/?", origin) is not None,
            "WsprryPi consumer checkout authority differs")
    require(not git(source, "status", "--porcelain=v1", "--untracked-files=all").strip(),
            "WsprryPi consumer source is dirty")
    require(all(line.startswith("H ") for line in git(source, "ls-files", "-v").splitlines()),
            "WsprryPi consumer source has hidden index state")
    commit, tree = git(source, "rev-parse", "HEAD", "HEAD^{tree}").splitlines()
    require(SHA40.fullmatch(commit) and SHA40.fullmatch(tree),
            "WsprryPi consumer Git identity is malformed")
    return {"sourceCommit": commit, "sourceTree": tree}


class DriveSelector(HTMLParser):
    inert_elements = {"template", "noscript", "textarea"}

    def __init__(self):
        super().__init__()
        self.matches = 0
        self.selected = False
        self.options = set()
        self.all_values = set()
        self.inert = []
        self.fieldsets = []

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if self.inert or tag in self.inert_elements:
            require(not self.selected, f"unsupported <{tag}> in RP1 drive selector")
            if tag in self.inert_elements:
                self.inert.append(tag)
            return
        if tag == "select" and ("id", SELECTOR_ID) in attrs:
            require(not self.selected and len(values) == len(attrs),
                    "ambiguous RP1 drive selector")
            require("disabled" not in values and "hidden" not in values and not any(self.fieldsets),
                    "RP1 drive selector is unavailable")
            self.selected = True
            self.matches += 1
        elif self.selected and tag == "option":
            value = values.get("value")
            require(len(values) == len(attrs) and isinstance(value, str)
                    and re.fullmatch(r"(?:0|[1-9][0-9]{0,5})", value),
                    "invalid RP1 drive option value")
            drive = int(value)
            require(drive not in self.all_values, "duplicate RP1 drive option")
            self.all_values.add(drive)
            if "disabled" not in values and "hidden" not in values:
                self.options.add(drive)
        elif self.selected:
            raise ContractError(f"unsupported <{tag}> in RP1 drive selector")
        elif tag == "fieldset":
            self.fieldsets.append("disabled" in values)

    def handle_endtag(self, tag):
        if self.inert:
            if tag == self.inert[-1]:
                self.inert.pop()
            return
        if tag == "select":
            self.selected = False
        elif tag == "fieldset" and self.fieldsets:
            self.fieldsets.pop()

    def handle_pi(self, data):
        require(not self.selected, "RP1 drive selector must expose literal option values")


def ui_choices(markup, script):
    selector = DriveSelector()
    selector.feed(markup)
    selector.close()
    require(selector.matches == 1 and not selector.selected and selector.options,
            "expected one complete RP1 drive selector")
    definitions = []
    for name in ("supportedRp1GpioDrive", "validateRp1GpioDrive", "syncGpioDriveControls"):
        matches = re.findall(r"^function " + name + r"\([^\n]*\) \{\n.*?^\}", script,
                             flags=re.MULTILINE | re.DOTALL)
        require(len(matches) == 1, f"expected one RP1 browser function {name}")
        definitions.extend(matches)
    fixture = """
const {readFileSync} = require('node:fs');
const vm = require('node:vm');
const {definitions, values} = JSON.parse(readFileSync(0, 'utf8'));
const validate = value => {
    const field = {
        value, disabled: true, validity: null,
        getAttribute: () => null,
        setCustomValidity(message) { this.validity = message; },
    };
    const group = {hidden: true};
    const elements = {'rp1_gpio_drive_ma': field, 'rp1-gpio-drive-group': group};
    const check = vm.runInNewContext(
        definitions + '\\nsyncGpioDriveControls(); validateRp1GpioDrive();',
        {
            document: {getElementById: id => elements[id] ?? null},
            selectedTransmitBackend: () => 'gpio',
            isRp1GpioPlatform: () => true,
            rp1GpioOperatorVisible: () => true,
            setFieldValidationState: () => {},
        },
        {timeout: 1000, contextCodeGeneration: {strings: false, wasm: false}},
    );
    return check === true && field.validity === '' && !field.disabled && !group.hidden;
};
process.stdout.write(JSON.stringify(values.map(validate)));
"""
    values = sorted(set(range(17)) | selector.all_values)
    result = subprocess.run(
        ["node", "--input-type=commonjs", "-e", fixture],
        input=json.dumps({"definitions": "\n".join(definitions), "values": [str(x) for x in values]}),
        encoding="utf-8", capture_output=True, check=True, timeout=10, env=PROBE_ENV,
    )
    flags = json.loads(result.stdout)
    require(isinstance(flags, list) and len(flags) == len(values)
            and all(type(flag) is bool for flag in flags), "invalid RP1 browser validation result")
    accepted = {drive for drive, flag in zip(values, flags) if flag}
    require(selector.options <= accepted,
            "RP1 browser validation rejects displayed drive choices "
            + ", ".join(map(str, sorted(selector.options - accepted))))
    return selector.options


def cli_choices(header):
    probe = """
#include "rp1_gpio_drive_options.hpp"
#include <iostream>
int main()
{
    if (kDefaultRp1GpioDriveMa != 2 || !is_supported_rp1_gpio_drive_ma(kDefaultRp1GpioDriveMa))
        return 1;
    for (int drive : kSupportedRp1GpioDriveMa) {
        if (!is_supported_rp1_gpio_drive_ma(drive)) return 1;
        std::cout << drive << '\\n';
    }
}
"""
    with tempfile.TemporaryDirectory(prefix="wsprrypi-rp1-drive-") as temporary:
        directory = Path(temporary)
        (directory / Path(CLI_OPTIONS).name).write_text(header, encoding="utf-8")
        source = directory / "probe.cpp"
        source.write_text(probe, encoding="utf-8")
        binary = directory / "probe"
        environment = {**PROBE_ENV, "TMPDIR": str(directory)}
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", source, "-o", binary],
                       check=True, capture_output=True, timeout=60, cwd=directory, env=environment)
        result = subprocess.run([binary], check=True, capture_output=True, text=True,
                                timeout=10, env=PROBE_ENV)
    values = list(map(int, result.stdout.splitlines()))
    require(values and len(set(values)) == len(values), "missing or duplicate RP1 CLI drive choices")
    return set(values)


def require_cli_wiring(source):
    def keep_literals(match):
        token = match.group()
        return token if token[0] != "/" else " " + "\n" * token.count("\n")

    source = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',
                    keep_literals, source)
    registration = re.findall(
        r'^\s*\{\s*"rp1-gpio-drive-ma"\s*,\s*required_argument\s*,\s*nullptr\s*,\s*(\d+)\s*\},',
        source, re.MULTILINE,
    )
    require(len(registration) == 1, "expected one production RP1 CLI drive option")
    cases = re.findall(r"^\s*case\s+" + registration[0] + r"\s*:\s*\{(.*?)(?=^\s*case\s)",
                       source, re.MULTILINE | re.DOTALL)
    require(len(cases) == 1 and all(re.search(pattern, cases[0]) for pattern in (
        r'parse_integer_option\s*\(\s*optarg\s*,\s*"--rp1-gpio-drive-ma"\s*\)',
        r'if\s*\(\s*!is_supported_rp1_gpio_drive_ma\s*\(\s*drive\s*\)\s*\)\s*'
        r'\{\s*print_usage\s*\(\s*"[^"\n]*"\s*,\s*EXIT_FAILURE\s*\)\s*;\s*\}',
        r"\bconfig\.rp1_gpio_drive_ma\s*=\s*drive\s*;",
    )), "production RP1 CLI does not use the checked drive choices")


def installer_tag(source):
    assignments = [node for node in ast.parse(source).body if isinstance(node, ast.Assign)
                   and any(isinstance(target, ast.Name) and target.id == "DEFAULT_RELEASE_TAG"
                           for target in node.targets)]
    require(len(assignments) == 1, "WsprryPi installer RP1 release pin is missing or ambiguous")
    value = assignments[0].value
    require(isinstance(value, ast.Constant) and isinstance(value.value, str),
            "WsprryPi installer RP1 release pin is malformed")
    return value.value


def observe(source, commit):
    def read(path):
        return git(source, "show", f"{commit}:{path}")

    require_cli_wiring(read(CLI_PARSER))
    return (ui_choices(read(UI_VIEW), read(UI_SCRIPT)), cli_choices(read(CLI_OPTIONS)),
            installer_tag(read(INSTALLER)), strict_json(read(REQUIREMENTS)))
