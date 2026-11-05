#!/usr/bin/env python3
"""Qualify network build reuse using actual compiler inputs and output bytes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import selectors
import shlex
import shutil
import signal
import stat
import subprocess
import time


def digest(file, *, deadline=None, clock=time.monotonic):
    result = hashlib.sha256()
    if deadline is not None and clock()>=deadline: raise TimeoutError('preprocessing input check exceeded its budget')
    descriptor = os.open(file,os.O_RDONLY|os.O_NONBLOCK)
    with os.fdopen(descriptor,'rb') as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise OSError('expected a regular build input')
        for block in iter(lambda: stream.read(1024*1024),b''):
            if deadline is not None and clock()>=deadline:
                raise TimeoutError('selected-file hashing exceeded the preprocessing budget')
            result.update(block)
    return result.hexdigest()


class PreprocessedInput:
    def __init__(self):
        self.digest = hashlib.sha256()
        self.pending = b''
        self.discard = False
        self.qualified = True
        self.files = set()

    def line(self, value):
        if not re.match(rb'^#\s+\d+\s',value): return
        marker = re.fullmatch(rb'#\s+\d+\s+("(?:[^"\\]|\\.)*")(?:\s+\d+)*\s*',value)
        try:
            literal = marker[1].decode() if marker else ''
            name = json.loads(literal)
            if json.dumps(name,ensure_ascii=False)!=literal: raise ValueError('unsupported C filename escape')
        except (ValueError,UnicodeError):
            self.qualified = False
            return
        if name==str(Path.cwd())+'//': return  # GCC -g working-directory marker, not a header.
        if name in ('<built-in>','<command-line>','<command line>'):
            self.qualified = self.qualified and not Path(name).exists()
        elif len(self.files)<10000: self.files.add(name)
        else: self.qualified = False

    def update(self, value):
        self.digest.update(value)
        pieces = value.split(b'\n')
        for index,piece in enumerate(pieces):
            if not self.discard:
                self.pending += piece
                if len(self.pending)>8192:
                    if self.pending.startswith(b'#'): self.qualified = False
                    self.pending,self.discard = b'',True
            if index<len(pieces)-1:
                if not self.discard: self.line(self.pending)
                self.pending,self.discard = b'',False

    def finish(self, deadline, clock):
        if clock()>=deadline: raise TimeoutError('preprocessing input check exceeded its budget')
        if self.pending and not self.discard: self.line(self.pending)
        headers = []
        for name in sorted(self.files):
            path = Path(name)
            try:
                headers.append([name,str(path.resolve(strict=True)),digest(path,deadline=deadline,clock=clock),path.stat().st_mtime_ns])
                if Path(name+'.gch').exists() or path.suffix in ('.gch','.pch'): self.qualified = False
            except TimeoutError: raise
            except (OSError,RuntimeError):
                self.qualified = False
                headers.append([name,'unavailable'])
        return [self.digest.hexdigest(),headers,self.qualified and bool(headers)]


def cleanup_failure(stage, primary, errors):
    details = '; '.join(f'{type(error).__name__}: {error}' for error in errors)
    raise RuntimeError(f'{stage}; cleanup_failures=[{details}]') from primary


def execute(command, stage, *, fingerprint=False, clock=time.monotonic):
    budget = 30 if fingerprint else 600
    started = clock()
    child, streams = None, None
    result, diagnostic = PreprocessedInput(), b''
    primary, errors = None, []
    try:
        child = subprocess.Popen(command,stdin=subprocess.DEVNULL,start_new_session=True,
            stdout=subprocess.PIPE if fingerprint else None,stderr=subprocess.PIPE if fingerprint else None)
        streams = selectors.DefaultSelector()
        if fingerprint:
            streams.register(child.stdout,selectors.EVENT_READ)
            streams.register(child.stderr,selectors.EVENT_READ)
            while streams.get_map():
                remaining = budget-(clock()-started)
                if remaining <= 0: raise subprocess.TimeoutExpired(command,budget)
                for key,_ in streams.select(remaining):
                    data = os.read(key.fileobj.fileno(),65536)
                    if not data: streams.unregister(key.fileobj)
                    elif key.fileobj is child.stdout: result.update(data)
                    else: diagnostic = (diagnostic+data)[-4096:]
        child.wait(timeout=max(0,budget-(clock()-started)))
        if child.returncode:
            raise ValueError(f'stage={stage}; expected=successful compiler exit; child_exit={child.returncode}; '+diagnostic.decode(errors='replace'))
        try: os.killpg(child.pid,0)
        except ProcessLookupError: pass
        else: raise ValueError(f'stage={stage}; compiler exited with owned descendants remaining')
        if fingerprint and diagnostic:
            os.write(2,b'stage=preprocessing; compiler diagnostic tail (4096 bytes):\n'+diagnostic)
        return result.finish(started+budget,clock) if fingerprint else None
    except BaseException as failure:
        primary = failure
        if isinstance(failure,(subprocess.TimeoutExpired,TimeoutError)):
            transition = 'compiler-exited' if child and child.poll() is not None else 'running'
            primary = TimeoutError(f'stage={stage}; expected=compiler and input-check completion; last_transition={transition}; '
                f'elapsed={clock()-started:.3f}s; hang_guard={budget}s; child_exit={child.poll() if child else None}')
        if child:
            try: os.killpg(child.pid,signal.SIGKILL)
            except ProcessLookupError: pass
            except BaseException as error: errors.append(error)
            try: child.wait(timeout=2)
            except BaseException as error: errors.append(error)
    finally:
        try:
            if streams: streams.close()
        except BaseException as error: errors.append(error)
        for pipe in ((child.stdout,child.stderr) if child else ()):
            try:
                if pipe: pipe.close()
            except BaseException as error: errors.append(error)
        if primary and errors: cleanup_failure('compiler operation and cleanup failed',primary,errors)
        if primary: raise primary
        if errors: cleanup_failure('stage=compiler-cleanup; resource closure failed',errors[0],errors)


def terminate(number, _frame):
    raise KeyboardInterrupt(f'stage=build-signal; received={number}')


def published(obj, dependencies):
    return {'object':digest(obj),'dependencies':digest(dependencies)}


def preprocess(command, flags, source):
    return [digest(source),execute(command+flags+['-E',str(source)],'preprocessing',fingerprint=True)]


def build(args):
    command, flags = shlex.split(args.compiler), shlex.split(args.flags)
    output = Path(args.object[0] if args.object else args.binary[0])
    metadata = output.with_suffix('.state') if args.object else Path(str(output)+'.state')
    temporary = Path(str(output)+'.tmp')
    state_temp = Path(str(metadata)+'.tmp')
    dependencies = output.with_suffix('.d')
    dep_temp = Path(str(dependencies)+'.tmp')
    primary = None
    try:
        if args.check and (not output.is_file() or not metadata.is_file()): return False
        if args.object:
            source = Path(args.object[1])
            inputs = None if args.conservative else preprocess(command,flags,source)
            current = {'identity':args.identity,'input':inputs}
            try:
                current.update(published(output,dependencies))
                if not args.conservative and inputs[1][2] and json.loads(metadata.read_text())==current: return True
            except (OSError,ValueError): pass
            if args.check: return False
            output.parent.mkdir(parents=True,exist_ok=True)
            execute(command+flags+['-MD','-MP','-MF',str(dep_temp),'-MT',str(output),
                '-c',str(source),'-o',str(temporary)],'compilation')
            if not dep_temp.read_text().startswith(str(output)+':'):
                raise ValueError('stage=dependency-publication; missing compiler target record')
            if not args.conservative and inputs!=preprocess(command,flags,source):
                raise ValueError('stage=dependency-publication; compiler inputs changed during compilation')
            current.update(published(temporary,dep_temp))
            state_temp.write_text(json.dumps(current))
            os.replace(dep_temp,dependencies)
        else:
            objects = args.binary[1:]
            if args.check and any(not Path(name).is_file() for name in [*objects,*args.file]): return False
            inputs = [(name,digest(name)) for name in [*objects,*args.file]]
            current = {'identity':args.identity,'input':inputs}
            try:
                current['binary'] = digest(output)
                # JSON restores tuples as lists; compare the serialized value.
                if not args.conservative and json.loads(metadata.read_text())==json.loads(json.dumps(current)): return True
            except (OSError,ValueError): pass
            if args.check: return False
            output.parent.mkdir(parents=True,exist_ok=True)
            execute(command+flags+objects+shlex.split(args.link)+['-o',str(temporary)],'linking')
            if inputs!=[(name,digest(name)) for name in [*objects,*args.file]]:
                raise ValueError('stage=link-publication; link inputs changed during linking')
            current['binary'] = digest(temporary)
            state_temp.write_text(json.dumps(current))
        os.replace(state_temp,metadata)
        os.replace(temporary,output)
        return True
    except BaseException as error:
        primary = ValueError(f'stage=dependency-publication; expected=compiler object and dependencies; {error}') if isinstance(error,OSError) else error
    finally:
        failures = []
        if not args.check:
            for path in (temporary,state_temp,dep_temp):
                try: path.unlink(missing_ok=True)
                except BaseException as error: failures.append(error)
        if primary and failures: cleanup_failure('build operation and cleanup failed',primary,failures)
        if primary: raise primary
        if failures: cleanup_failure('stage=build-cleanup; temporary removal failed',failures[0],failures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--invalid',nargs='+')
    parser.add_argument('--object',nargs=2,metavar=('OBJECT','SOURCE'))
    parser.add_argument('--binary',nargs='+')
    parser.add_argument('--identity')
    parser.add_argument('--check',action='store_true')
    parser.add_argument('--conservative',action='store_true')
    parser.add_argument('--compiler')
    parser.add_argument('--version')
    parser.add_argument('--flags')
    parser.add_argument('--link')
    parser.add_argument('--file',action='append',default=[])
    args = parser.parse_args()
    if args.invalid:
        invalid = []
        for name in args.invalid:
            obj = Path(name)
            try:
                saved = json.loads(obj.with_suffix('.state').read_text())
                valid = all(saved.get(key)==value for key,value in published(obj,obj.with_suffix('.d')).items())
            except (OSError,ValueError,AttributeError): valid = False
            if not valid: invalid.append(name)
        print(' '.join(invalid))
        return
    if args.object or args.binary:
        signal.signal(signal.SIGTERM,terminate)
        signal.signal(signal.SIGINT,terminate)
        try:
            if not build(args): raise SystemExit(1)
        except (ValueError,OSError) as error: parser.error(str(error))
        return
    if any(value is None for value in (args.compiler,args.version,args.flags,args.link)):
        parser.error('stage=build-identity; missing compiler/flags/link identity')
    command = shlex.split(args.compiler)
    if not command or not shutil.which(command[0]):
        parser.error('stage=build-identity; expected=available compiler executable')
    files = [Path(__file__), *map(Path, args.file)]
    def argument_files(arguments, active=()):
        for argument in arguments:
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
    conservative = not args.version or any(argument.endswith(('.pch','.gch')) for argument in compile_arguments)
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
                re.fullmatch(r'-std=c\+\+\d+|-pthread|-O[0-3sgz]?|-g[0-2]?|-W[\w=+-]+|-fPIC|-fPIE|-pipe',argument)):
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
    for file in files:
        identity.append([str(file.resolve(strict=True)), digest(file)])
    print(hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()+
          ('-conservative' if conservative else ''))


if __name__ == '__main__':
    main()
