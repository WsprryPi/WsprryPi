#!/usr/bin/env python3
"""Compile both production object-rule variants and verify header invalidation."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT/'src/Makefile').read_text()
block = source[source.index('DEPENDENCY_OBJECTS :='):source.index('$(MISSING_DEP_OBJECTS): FORCE_OBJECT_DEPENDENCIES') + len('$(MISSING_DEP_OBJECTS): FORCE_OBJECT_DEPENDENCIES')]
rules = []
for mode in ('DEBUG', 'RELEASE'):
    marker = '$(OBJ_DIR_' + mode + ')/%.o: %.cpp'
    start = source.index(marker)
    rules.append(source[start:source.index('\n\n', start)])
compiler = shutil.which('c++')
assert compiler
with tempfile.TemporaryDirectory(prefix='phase115-make-deps-') as temp:
    root = Path(temp)
    (root/'probe.cpp').write_text('#include "shape.hpp"\nint main() { return sizeof(Shape); }\n')
    (root/'shape.hpp').write_text('struct Shape { char data[8]; };\n')
    makefile = '''Q := @
PROFILE ?= one
OBJ_DIR_DEBUG := build/debug/$(PROFILE)
OBJ_DIR_RELEASE := build/release/$(PROFILE)
CPP_DEBUG_OBJECTS := $(OBJ_DIR_DEBUG)/probe.o
CPP_OBJECTS := $(OBJ_DIR_RELEASE)/probe.o
CXX := ''' + compiler + '''
CXX_DEBUG_FLAGS := -MMD -MP -g
CXX_RELEASE_FLAGS := -MMD -MP -O2
.DEFAULT_GOAL := all
.PHONY: all
all: $(CPP_DEBUG_OBJECTS) $(CPP_OBJECTS)
''' + block + '\n' + '\n\n'.join(rules) + '\n'
    (root/'Makefile').write_text(makefile)
    env = os.environ.copy()
    for key in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'): env.pop(key,None)
    def make(profile):
        r=subprocess.run(['make','all','PROFILE='+profile],cwd=root,env=env,text=True,capture_output=True)
        assert r.returncode == 0,(r.stdout,r.stderr)
        return r.stdout
    for profile in ('one','two'):
        assert make(profile).count('Compiling') == 2
        assert 'Compiling' not in make(profile)
    time.sleep(1.05)
    (root/'shape.hpp').write_text('struct Shape { char data[16]; };\n')
    for profile in ('one','two'):
        assert make(profile).count('Compiling') == 2
        for mode in ('debug','release'):
            obj=root/'build'/mode/profile/'probe.o'
            subprocess.run([compiler,str(obj),'-o',str(root/'probe')],check=True)
            assert subprocess.run([str(root/'probe')]).returncode == 16
            assert obj.with_suffix('.d').is_file()
    (root/'build/debug/one/probe.d').unlink()
    assert make('one').count('Compiling') == 1
    assert 'Compiling' not in make('one')
print('Production Make rules: debug/release, two profiles, header ABI refresh and missing-dependency migration passed')

# Directly linked test executables do not use the production object-rule .d files.
# Exercise their actual Make rules with minimal sources to protect clock changes.
with tempfile.TemporaryDirectory(prefix='rp1-make-clock-deps-') as temp:
    root = Path(temp)
    executable_rules = []
    targets = ('RP1_GPCLK_PLANNER_TEST_OUT', 'RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT')
    for target in targets:
        start = source.index('$(BIN_DIR)/$(' + target + '):')
        rule = source[start:source.index('\n\n', start)]
        executable_rules.append(rule)
        for name in rule.splitlines()[0].split(':', 1)[1].split():
            path = root/name
            path.parent.mkdir(parents=True, exist_ok=True)
            if path.suffix == '.cpp' and path.name in (
                    'rp1_gpclk_planner_test.cpp', 'rp1_gpclk_transmit_backend_test.cpp'):
                path.write_text('#include "rp1_gpclk_planner.hpp"\n'
                    'int main() { return parent_hz == 200000000 ? 0 : 7; }\n')
            else:
                path.write_text('// Hardware-free Make-rule fixture.\n')
    header = root/'WSPR-Transmitter/src/rp1_gpclk_planner.hpp'
    header.write_text('inline constexpr int parent_hz = 200000000;\n')
    (root/'Makefile').write_text('''Q := @
BIN_DIR := build/bin
RP1_GPCLK_PLANNER_TEST_OUT := planner
RP1_GPCLK_TRANSMIT_BACKEND_TEST_OUT := adapter
CXX := ''' + compiler + '''
RP1_GPCLK_PLANNER_TEST_FLAGS := -std=c++20 -IWSPR-Transmitter/src
.DEFAULT_GOAL := all
.PHONY: all
all: build/bin/planner build/bin/adapter
''' + '\n\n'.join(executable_rules) + '\n')
    def make_executables():
        result = subprocess.run(['make', 'all'], cwd=root, env=env,
                                text=True, capture_output=True)
        assert result.returncode == 0, (result.stdout, result.stderr)
    make_executables()
    binaries = [root/'build/bin'/name for name in ('planner', 'adapter')]
    before = [path.stat().st_mtime_ns for path in binaries]
    make_executables()
    assert before == [path.stat().st_mtime_ns for path in binaries]
    for parent, expected_exit in ((50000000, 7), (200000000, 0)):
        time.sleep(1.05)
        header.write_text(f'inline constexpr int parent_hz = {parent};\n')
        make_executables()
        for binary in binaries:
            assert subprocess.run([str(binary)]).returncode == expected_exit, binary
print('RP1 executable Make rules: planner and adapter clock-header invalidation passed')
