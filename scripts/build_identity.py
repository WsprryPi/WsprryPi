#!/usr/bin/env python3
"""Identify network build inputs and validate published object/dependency pairs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil


def digest(file):
    result = hashlib.sha256()
    with Path(file).open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            result.update(block)
    return result.hexdigest()


def state(obj, dependencies):
    return {'object': digest(obj), 'dependencies': digest(dependencies)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--invalid', nargs='+')
    parser.add_argument('--record', nargs=3, metavar=('OBJECT', 'DEPENDENCIES', 'OUTPUT'))
    parser.add_argument('--compiler')
    parser.add_argument('--version')
    parser.add_argument('--flags')
    parser.add_argument('--link')
    parser.add_argument('--file', action='append', default=[])
    args = parser.parse_args()
    if args.invalid:
        invalid = []
        for name in args.invalid:
            obj = Path(name)
            try:
                valid = json.loads(obj.with_suffix('.state').read_text()) == state(obj,obj.with_suffix('.d'))
            except (OSError, ValueError):
                valid = False
            if not valid: invalid.append(name)
        print(' '.join(invalid))
        return
    if args.record:
        obj, dependencies, output = map(Path,args.record)
        if not dependencies.read_text().startswith(str(obj).removesuffix('.tmp')+':'):
            parser.error('stage=dependency-publication; missing compiler target record')
        output.write_text(json.dumps(state(obj,dependencies)))
        return
    if any(value is None for value in (args.compiler,args.version,args.flags,args.link)):
        parser.error('stage=build-identity; missing compiler/flags/link identity')
    command = shlex.split(args.compiler)
    if not command or not shutil.which(command[0]) or not args.version:
        parser.error('stage=build-identity; expected=available identified compiler')
    files = [Path(__file__), *map(Path, args.file)]
    flattened = []
    def argument_files(arguments, active=()):
        for argument in arguments:
            flattened.append(argument)
            if argument.startswith('@'):
                response = Path(argument[1:]).resolve(strict=True)
                if response in active or len(active) >= 16:
                    parser.error('stage=build-identity; recursive compiler response file')
                files.append(response)
                argument_files(shlex.split(response.read_text()), (*active,response))
            else:
                value = argument.split('=',1)[-1]
                if Path(value).is_file() or (not argument.startswith('-') and
                        Path(value).suffix in ('.a', '.so', '.dylib', '.o')):
                    files.append(Path(value))
    argument_files(shlex.split(args.flags)+shlex.split(args.link))
    argument_files(command)
    compile_arguments = []
    # Linker syntax has its own conservative relink policy in the Make rules.
    # Only compiler arguments participate in compile-reuse admission.
    def compiler_arguments(arguments):
        for argument in arguments:
            if argument.startswith('@'):
                compiler_arguments(shlex.split(Path(argument[1:]).read_text()))
            else:
                compile_arguments.append(argument)
    compiler_arguments(shlex.split(args.flags)+command[1:])
    conservative = False
    skip = False
    search_options = ('-I','-isystem','-iquote','-idirafter')
    for index, argument in enumerate(compile_arguments):
        if skip:
            skip = False
            continue
        if argument in ('-I','-isystem','-iquote','-idirafter','-include','-imacros','-D','-U'):
            skip = True
            if argument in search_options and index+1 < len(compile_arguments):
                conservative = conservative or compile_arguments[index+1].startswith(('=','$','-'))
        elif argument.startswith(('-I','-isystem','-iquote','-idirafter','-D','-U')):
            for option in search_options:
                if argument.startswith(option):
                    conservative = conservative or argument[len(option):].startswith(('=','$','-'))
        elif argument.startswith('-') and (argument.startswith(('-Wl,','-Wp,','-Wa,')) or not
                re.fullmatch(r'-std=c\+\+\d+|-pthread|-O[0-3sgz]?|-g[0-3]?|-W[\w=+-]+|-fPIC|-fPIE|-pipe',argument)):
            conservative = True
    for argument in command:
        resolved = shutil.which(argument) if not argument.startswith('-') else None
        if resolved:
            files.append(Path(resolved))
        elif Path(argument).is_file():
            files.append(Path(argument))
    identity = [str(Path.cwd().resolve()), platform.system(), platform.machine(), vars(args),
        {key: os.environ.get(key) for key in ('PATH', 'CPATH', 'C_INCLUDE_PATH',
        'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'COMPILER_PATH', 'GCC_EXEC_PREFIX',
        'SDKROOT', 'DEVELOPER_DIR', 'MACOSX_DEPLOYMENT_TARGET', 'LANG', 'LC_ALL')}]
    # A newly added earlier header can shadow an unchanged dependency. Record
    # header names in the actual search roots, not just previously selected files.
    includes = []
    for index, argument in enumerate(flattened):
        for option in ('-I','-isystem','-iquote','-idirafter'):
            if argument == option and index+1 < len(flattened):
                includes.append(flattened[index+1])
            elif argument.startswith(option) and argument != option:
                includes.append(argument[len(option):])
    for key in ('CPATH','C_INCLUDE_PATH','CPLUS_INCLUDE_PATH'):
        if os.environ.get(key): includes.extend(os.environ[key].split(os.pathsep))
    for name in sorted(set(includes)):
        if name.startswith(('=','$')):
            conservative = True
            continue
        root = Path(name or '.').resolve()
        headers = []
        incomplete = []
        entries = 0
        for directory, children, names in os.walk(root,onerror=incomplete.append):
            entries += len(children)+len(names)
            if entries > 50000:
                conservative = True
                break
            path = Path(directory)
            children[:] = sorted(child for child in children if child not in
                ('.git','node_modules','__pycache__') and not
                ('build' in path.parts and child in ('obj','bin','incremental','CMakeFiles','.cmake')))
            headers.extend(str(path/file) for file in sorted(names) if
                Path(file).suffix in ('.h','.hpp','.hxx','.hh','.inc','.tpp') or
                (not Path(file).suffix and 'build' not in path.parts))
        identity.append([str(root),headers])
        conservative = conservative or bool(incomplete)
    for file in files:
        identity.append([str(file.resolve(strict=True)), digest(file)])
    print(hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()+
          ('-conservative' if conservative else ''))


if __name__ == '__main__':
    main()
