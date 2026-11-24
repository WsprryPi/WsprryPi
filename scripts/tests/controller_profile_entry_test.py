#!/usr/bin/env python3
"""Exercise production role admission before runtime construction or storage."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix='controller-entry-') as temporary:
        directory = Path(temporary)
        header = directory / 'backend_capabilities.hpp'
        subprocess.run(['python3', str(ROOT / 'scripts/generate_backend_capabilities.py'),
                        '--profile', 'controller', '--backends', '', '--ancillary-gpio', '0',
                        '--output', str(header)], check=True)
        source = directory / 'entry.cpp'
        source.write_text('#include "controller_profile_entry.hpp"\n#include <string>\n'
                          'std::string get_version_string(){return "test version";}\n'
                          'int main(int argc,char **argv){return controller_profile_entry(argc,argv).value_or(123);}\n')
        binary = directory / 'entry'
        subprocess.run([*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(directory), '-I' + str(ROOT / 'src'), str(source),
                        str(ROOT / 'src/controller_profile_entry.cpp'), '-o', str(binary)], check=True)

        def run(arguments, expected):
            result = subprocess.run([str(binary), *arguments], capture_output=True, text=True, timeout=5)
            assert result.returncode == expected, (arguments, result)
            return result

        for arguments in ([], ['--backend', 'simulated'], ['AA0NT', 'EM18', '20', '40m'],
                          ['--initialize-state'], ['--maintenance-worker', '--role', 'pi'],
                          ['--controller-only', '--help', '--backend', 'simulated'], ['--web-service', '--maintenance']):
            run(arguments, 2)
        for arguments in (['--controller-only'], ['--controller-only', '--state-directory', '/private/state'],
                          ['--initialize-state', '--controller-only'], ['--settings', '--help'],
                          ['--maintenance', 'help'], ['--web-service'], ['--maintenance-worker', '--role', 'controller']):
            run(arguments, 123)
        for arguments in (['--help'], ['--controller-only', '--help'], ['--version', '--controller-only']):
            result = run(arguments, 0)
            assert 'Build profile: controller' in result.stdout
        assert run(['--list-backends'], 0).stdout == '\n'
        # The ordinary transmitter profile retains its existing parser/CLI owner.
        subprocess.run(['python3', str(ROOT / 'scripts/generate_backend_capabilities.py'),
                        '--profile', 'transmitter', '--backends', 'simulated', '--ancillary-gpio', '0',
                        '--output', str(header)], check=True)
        subprocess.run([*shlex.split(os.environ.get('CXX', 'c++')), '-std=c++20',
                        '-I' + str(directory), '-I' + str(ROOT / 'src'), str(source),
                        str(ROOT / 'src/controller_profile_entry.cpp'), '-o', str(binary)], check=True)
        for arguments in ([], ['--help'], ['--backend', 'simulated'], ['--controller-only']):
            run(arguments, 123)
        print('PASS controller profile early role refusal, safe help/version, and independent command dispatch')


if __name__ == '__main__':
    main()
