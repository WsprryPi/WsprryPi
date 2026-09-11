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
