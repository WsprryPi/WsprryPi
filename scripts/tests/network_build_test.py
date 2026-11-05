#!/usr/bin/env python3
"""Exercise included production network rules with a real compiler and gated faults."""
import json
import os
from pathlib import Path
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest

from fixture_commands import run_shell

ROOT = Path(__file__).resolve().parents[2]


class NetworkBuild(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='wsprrypi-network-build-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.src = self.root / 'src'
        self.src.mkdir()
        scripts = self.root / 'scripts'
        scripts.mkdir()
        for name in ('network_build_rules.mk', 'build_identity.py'):
            shutil.copyfile(ROOT / 'scripts' / name, scripts / name)
        # Ask Make for the production source set; line wrapping/assignment
        # notation in the included rules is not a test oracle.
        discovery = self.src/'discover.mk'
        discovery.write_text('NETWORK_BUILD_ID := nonbuild\nNETWORK_COMPILE_ID := nonbuild\n'
            'include ../scripts/network_build_rules.mk\nprint-sources:\n'
            "\t@printf '%s\\n' $(sort $(foreach name,$(NETWORK_NAMES),$($(name)_SOURCES)))\n")
        environment = {k:v for k,v in os.environ.items() if k not in ('MAKEFLAGS','MFLAGS','MAKELEVEL')}
        result = run_shell('make --no-print-directory -f discover.mk print-sources',
            cwd=self.src,env=environment,stage='discover-network-sources',watchdog=10)
        self.assertEqual(result.returncode,0,result.stderr)
        sources = set(result.stdout.splitlines())
        self.assertEqual(len(sources),16)
        for name in sources:
            path = self.src / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "shared.hpp"\n' + (
                '#include <cstdio>\n#include <cstdlib>\nint main() { '
                'FILE* f=fopen(getenv("RUN_LOG"),"a"); if(!f) return 90; '
                f'fprintf(f,"{path.stem}\\n"); fclose(f); return LEVEL+PROFILE; }}\n'
                if name.startswith('tests/') else ''))
        (self.src / 'shared.hpp').write_text('#include "transitive.hpp"\n#ifndef PROFILE\n#define PROFILE 0\n#endif\n')
        (self.src / 'transitive.hpp').write_text('#define LEVEL 0\n')
        for path in self.src.rglob('*'):
            if path.is_file():
                stamp = path.stat().st_mtime_ns-3_600_000_000_000
                os.utime(path, ns=(stamp,stamp))
        self.compiler = self.root / 'compiler.py'
        self.compiler.write_text('''import json, os, sys
args=sys.argv[1:]
if '-o' in args:
 with open(os.environ['COMPILE_LOG'],'a') as log: log.write(json.dumps(args)+'\\n')
 phase='compile' if '-c' in args else 'link'
 if os.environ.get('FAIL_PHASE')==phase:
  open(args[args.index('-o')+1],'w').write('partial')
  print('stage='+phase+'; deliberate failure',file=sys.stderr);sys.exit(27)
 if '-c' in args and args[args.index('-c')+1]==os.environ.get('GATE_SOURCE'):
  with open(os.environ['READY'],'w') as ready: ready.write('entered\\n')
  with open(os.environ['RELEASE']) as release:
   if release.read(1)!='x': sys.exit(28)
os.execv('''+repr(shutil.which('c++'))+''',['c++',*args])
''')
        parent = (ROOT / 'src/Makefile').read_text()
        owners = []
        for target in ('wtp-api-cpp-test', 'wtp-config-cpp-test', 'wtp-plain-tcp-test',
                       'wtp-usb-management-test', 'wtp-tls-probe-build', 'wtp-tls-build', 'wtp-tls-test'):
            start = parent.index('\n'+target+':')+1
            owners.append(parent[start:parent.index('\n\n', start)])
        (self.src / 'Makefile').write_text('Q := @\nWTP_NETWORK_BUILD_DIR := build/network\n'
            'CXX := '+shlex.join([sys.executable, str(self.compiler)])+'\n'
            'CXX_VERSION_LINE := fixture compiler backed by '+str(shutil.which('c++'))+'\n'
            'WTP_NETWORK_FLAGS := -std=c++20 -I. $(EXTRA_FLAGS)\n'
            'OPENSSL_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)\n'
            'include ../scripts/network_build_rules.mk\n'
            '.PHONY: wtp-api-cpp-test wtp-config-cpp-test wtp-plain-tcp-test wtp-usb-management-test help clean\n'
            '.PHONY: wtp-tls-probe-build wtp-tls-build wtp-tls-test wtp-network-client-build\n'
            'wtp-network-client-build:\n\t@printf "%s\\n" interop-build >> interop-calls\n'
            'help:\n\t@true\nclean:\n\t@rm -rf build\n'+ '\n\n'.join(owners)+'\n')
        self.env = os.environ.copy()
        for key in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
            self.env.pop(key, None)
        self.env.update(COMPILE_LOG=str(self.root/'commands'), RUN_LOG=str(self.root/'runs'))
        (self.src/'tests/wtp_tls_test.py').write_text('import os, pathlib, subprocess, sys\n'
            'root=pathlib.Path(sys.argv[1])\n'
            'for name in ("wtp_tls_probe","wtp_tls_contract_test"):\n'
            ' subprocess.run([str(root.resolve()/name)],check=True,timeout=5)\n')

    def make(self, *goals, **env):
        return run_shell(shlex.join(['make', '--no-print-directory', *goals]), cwd=self.src,
            env=self.env | env, stage='network-build-regression', watchdog=60)

    def passed(self, *goals, **env):
        result = self.make(*goals, **env)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        return result

    def commands(self):
        file = self.root/'commands'
        return [json.loads(line) for line in file.read_text().splitlines()] if file.exists() else []

    def reset_log(self):
        (self.root/'commands').unlink(missing_ok=True)

    def dirty(self, name):
        path = self.src/name
        path.write_text(path.read_text()+'\n')
        # Establish prerequisite ordering, without sleeps or future timestamps.
        stamp = path.stat().st_mtime_ns-2_000_000_000
        for output in (self.src/'build').rglob('*'):
            if output.suffix == '.o' or output.parent.name == 'bin':
                os.utime(output, ns=(stamp, stamp))

    def test_cold_warm_shared_objects_and_required_execution(self):
        self.passed('-j2', 'wtp-network-tools-build', 'wtp-usb-management-test')
        commands = self.commands()
        self.assertEqual(sum('-c' in c for c in commands), 16)
        self.assertEqual(sum('-c' not in c for c in commands), 6)
        self.reset_log()
        self.passed('-j2', 'wtp-network-tools-build')
        self.assertEqual(self.commands(), [])
        (self.root/'runs').unlink()
        goals = ('wtp-api-cpp-test', 'wtp-config-cpp-test', 'wtp-plain-tcp-test', 'wtp-usb-management-test')
        for _ in range(2): self.passed(*goals)
        self.assertEqual(self.commands(), [])
        self.assertEqual(len((self.root/'runs').read_text().splitlines()), 8)

    def test_tls_runs_without_unused_interop_build_but_public_aggregate_keeps_it(self):
        for _ in range(2): self.passed('wtp-tls-test')
        self.assertFalse((self.src/'interop-calls').exists())
        self.assertEqual(len((self.root/'runs').read_text().splitlines()), 4)
        self.reset_log()
        self.passed('wtp-tls-build')
        self.assertEqual((self.src/'interop-calls').read_text().splitlines(), ['interop-build'])
        self.assertEqual(self.commands(), [])

    def test_sources_transitive_headers_deleted_inputs_and_dependency_metadata(self):
        self.passed('wtp-network-tools-build')
        self.reset_log()
        self.dirty('wtp_integration/tls.cpp')
        self.passed('wtp-network-tools-build')
        self.assertEqual(sum('-c' in c for c in self.commands()), 1)
        self.reset_log()
        self.dirty('transitive.hpp')
        self.passed('wtp-network-tools-build')
        self.assertEqual(sum('-c' in c for c in self.commands()), 14)
        self.reset_log()
        next((self.src/'build').rglob('tls.d')).unlink()
        self.passed('wtp-network-tools-build')
        self.assertEqual(sum('-c' in c for c in self.commands()), 1)
        next((self.src/'build').rglob('tls.d')).unlink()
        (self.src/'wtp_integration/tls.cpp').unlink()
        self.assertNotEqual(self.make('wtp-network-tools-build').returncode, 0)
        (self.src/'transitive.hpp').unlink()
        self.assertNotEqual(self.make('wtp-network-unit-build').returncode, 0)

    def test_flags_compiler_and_switch_back_select_correct_artifact(self):
        self.passed('wtp-api-cpp-test')
        self.assertNotEqual(self.make('wtp-api-cpp-test', 'EXTRA_FLAGS=-DPROFILE=1').returncode, 0)
        self.reset_log()
        self.passed('wtp-api-cpp-test')
        self.assertEqual(self.commands(), [])
        self.compiler.write_text(self.compiler.read_text()+'\n# changed compiler adapter\n')
        self.passed('wtp-api-cpp-test')
        self.assertEqual(sum('-c' in c for c in self.commands()), 3)

    def test_corrupted_dependency_or_object_state_cannot_admit_stale_code(self):
        for damage in ('empty','missing-header','malformed','missing-state','object'):
            with self.subTest(damage=damage):
                self.passed('clean')
                self.passed('wtp-api-cpp-test')
                dep = next((self.src/'build').rglob('wtp_browser_api_test.d'))
                if damage == 'empty': dep.write_text('')
                elif damage == 'missing-header':
                    dep.write_text(dep.read_text().replace('transitive.hpp','shared.hpp'))
                elif damage == 'malformed': dep.write_text('invalid makefile syntax\n')
                elif damage == 'missing-state': dep.with_suffix('.state').unlink()
                else: dep.with_suffix('.o').write_bytes(b'incomplete object')
                (self.src/'transitive.hpp').write_text('#define LEVEL 1\n')
                self.dirty('transitive.hpp')
                self.reset_log()
                result = self.make('wtp-api-cpp-test')
                self.assertNotEqual(result.returncode, 0, 'changed header must reach executed main')
                self.assertTrue(any('-c' in c and 'tests/wtp_browser_api_test.cpp' in c for c in self.commands()))
                self.assertEqual(len((self.root/'runs').read_text().splitlines()), 2)
                self.assertEqual(subprocess.run([str(self.src/'build/network/wtp_browser_api_test')],
                    env=self.env,timeout=5).returncode, 1)
                (self.root/'runs').unlink()
                (self.src/'transitive.hpp').write_text('#define LEVEL 0\n')

    def test_library_and_generated_header_changes_invalidate(self):
        library = self.src/'libfixture.a'
        code = self.src/'library.cpp'
        obj = self.src/'library.o'
        code.write_text('int fixture_value() { return 1; }\n')
        def archive():
            subprocess.run([shutil.which('c++'),'-c',str(code),'-o',str(obj)],check=True,timeout=30)
            subprocess.run(['ar','rcs',str(library),str(obj)],check=True,timeout=10)
        archive()
        flags = 'WTP_NETWORK_LDFLAGS='+str(library)
        self.passed('wtp-network-unit-build', flags)
        self.reset_log()
        self.passed('wtp-network-unit-build', flags)
        self.assertEqual(sum('-c' in c for c in self.commands()), 0)
        self.assertEqual(len(self.commands()), 2)
        code.write_text('int fixture_value() { return 2; }\n')
        archive()
        # Additional caller-selected library is a link input, not an object flag.
        # Record it in the identity alongside system libraries, even with unchanged path.
        self.passed('wtp-network-unit-build', flags)
        self.assertTrue(self.commands(), 'changed linked library must invalidate')
        self.assertEqual(sum('-c' in c for c in self.commands()), 0)
        generated = self.src/'generated.hpp'
        generated.write_text('#define GENERATED_VALUE 0\n')
        (self.src/'shared.hpp').write_text((self.src/'shared.hpp').read_text()+'#include "generated.hpp"\n')
        self.dirty('shared.hpp')
        self.passed('wtp-network-unit-build',flags)
        self.reset_log()
        self.dirty('generated.hpp')
        self.passed('wtp-network-unit-build',flags)
        self.assertTrue(any('-c' in c for c in self.commands()))
        generated.unlink()
        self.assertNotEqual(self.make('wtp-network-unit-build',flags).returncode, 0)

    def test_custom_library_search_and_response_files_cannot_hide_changes(self):
        code, obj, library = (self.src/name for name in ('library.cpp','library.o','libfixture.a'))
        main = self.src/'tests/wtp_browser_api_test.cpp'
        main.write_text('extern int fixture_value();\n'+main.read_text().replace('return LEVEL+PROFILE;', 'return LEVEL+PROFILE+fixture_value();'))
        flags = 'WTP_NETWORK_LDFLAGS=-L'+str(self.src)+' -lfixture'
        for value in (0,1,0):
            code.write_text(f'int fixture_value() {{ return {value}; }}\n')
            subprocess.run([shutil.which('c++'),'-c',str(code),'-o',str(obj)],check=True,timeout=30)
            subprocess.run(['ar','rcs',str(library),str(obj)],check=True,timeout=10)
            result = self.make('wtp-api-cpp-test',flags)
            self.assertEqual(result.returncode == 0, value == 0, result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(self.src/'build/network/wtp_browser_api_test')],
                env=self.env,timeout=5).returncode, value)

        # An unchanged response-file pathname must not conceal changed definitions.
        response = self.src/'flags.rsp'
        for value in (0,1,0):
            response.write_text(f'-DPROFILE={value}\n')
            result = self.make('wtp-api-cpp-test',flags,'EXTRA_FLAGS=@flags.rsp')
            self.assertEqual(result.returncode == 0, value == 0, result.stdout+result.stderr)
            self.assertEqual(subprocess.run([str(self.src/'build/network/wtp_browser_api_test')],
                env=self.env,timeout=5).returncode, value)

    def test_new_header_in_earlier_include_directory_changes_executed_code(self):
        early, late = self.root/'early', self.root/'late'
        early.mkdir()
        late.mkdir()
        (late/'choice.hpp').write_text('#define CHOICE 0\n')
        main = self.src/'tests/wtp_browser_api_test.cpp'
        main.write_text('#include "choice.hpp"\n'+main.read_text().replace('return LEVEL+PROFILE;', 'return LEVEL+PROFILE+CHOICE;'))
        for option in ('-I','-idirafter'):
            with self.subTest(option=option):
                flags = f'EXTRA_FLAGS={option} {early} {option} {late}'
                self.passed('wtp-api-cpp-test',flags)
                (early/'choice.hpp').write_text('#define CHOICE 1\n')
                result = self.make('wtp-api-cpp-test',flags)
                self.assertNotEqual(result.returncode, 0, 'new earlier header must invalidate selected main')
                self.assertEqual(subprocess.run([str(self.src/'build/network/wtp_browser_api_test')],
                    env=self.env,timeout=5).returncode, 1)
                (early/'choice.hpp').unlink()
                self.passed('wtp-api-cpp-test',flags)

    def test_unknown_compiler_option_retains_conservative_compilation(self):
        for option in ('-fno-inline','-I=unselected-sysroot-path'):
            with self.subTest(option=option):
                self.passed('wtp-api-cpp-test','EXTRA_FLAGS='+option)
                self.reset_log()
                self.passed('wtp-api-cpp-test','EXTRA_FLAGS='+option)
                self.assertEqual(sum('-c' in c for c in self.commands()),3)
                self.assertEqual(sum('-c' not in c for c in self.commands()),1)

    def test_compiler_and_linker_failure_never_publish_partial_artifacts(self):
        for phase in ('compile', 'link'):
            with self.subTest(phase=phase):
                self.passed('clean')
                result = self.make('wtp-api-cpp-test', FAIL_PHASE=phase)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('stage='+phase+'; deliberate failure', result.stderr)
                self.assertFalse((self.src/'build/network/wtp_browser_api_test').exists())
                self.assertEqual(list((self.src/'build').rglob('*.tmp')), [])
                self.passed('wtp-api-cpp-test')

    def test_introspection_does_not_mutate(self):
        self.passed('clean','CXX=unavailable-fixture-compiler')
        self.passed('help','CXX=unavailable-fixture-compiler')
        for goal in (('-n','wtp-network-tools-build'), ('-q','wtp-network-tools-build'), ('help',)):
            result = self.make(*goal)
            self.assertIn(result.returncode, (0,1))
            self.assertFalse((self.src/'build').exists())
            self.assertEqual(self.commands(), [])
        self.passed('wtp-network-tools-build')
        self.reset_log()
        before = {str(p): (p.stat().st_mtime_ns,p.read_bytes()) for p in (self.src/'build').rglob('*') if p.is_file()}
        self.passed('-q','wtp-network-tools-build')
        after = {str(p): (p.stat().st_mtime_ns,p.read_bytes()) for p in (self.src/'build').rglob('*') if p.is_file()}
        self.assertEqual(before, after)
        self.assertEqual(self.commands(), [])

    def test_actual_ci_job_budget_and_low_memory_limits(self):
        base = ['make','--no-print-directory','ci-build-budget',
            'BACKENDS=simulated','ANCILLARY_GPIO=0','GUI_CLAMP=0','HOST_OS=Linux',
            'NPROCS=32','MEM_KB=16777216','MEM_AVAIL_KB=16777216','SWAP_KB=0',
            'GPIOD_CPP_HEADER_AVAILABLE=0','BUILD_METADATA_DIR='+str(self.root/'parent-generated'),
            'BUILD_METADATA_HEADER='+str(self.root/'parent-generated/build_metadata.hpp')]
        output = self.root/'github-env'
        for extra, expected in (([],4),(['BUILD_JOB_LIMIT=2'],2),
                (['MEM_AVAIL_KB=262144'],1),(['NPROCS=1'],1),(['JOBS=2'],2)):
            output.unlink(missing_ok=True)
            result = run_shell(shlex.join(base+extra),cwd=ROOT/'src',
                env=self.env|{'GITHUB_ENV':str(output)},stage='ci-build-budget',watchdog=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(output.read_text(),'MAKEFLAGS=-j'+str(expected)+'\n')
        for extra in (['BUILD_JOB_LIMIT=0'],['JOBS=7']):
            output.unlink(missing_ok=True)
            result = run_shell(shlex.join(base+extra),cwd=ROOT/'src',
                env=self.env|{'GITHUB_ENV':str(output)},stage='ci-build-budget-refusal',watchdog=30)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse(output.exists())
        result = run_shell(shlex.join(base),cwd=ROOT/'src',env=self.env|{'GITHUB_ENV':''},
            stage='ci-build-budget-missing-output',watchdog=30)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('stage=build-budget',result.stderr)
        result = run_shell(shlex.join(base),cwd=ROOT/'src',env=self.env|{'GITHUB_ENV':str(self.root)},
            stage='ci-build-budget-output-failure',watchdog=30)
        self.assertNotEqual(result.returncode,0)

    def jobserver_probe(self):
        worker = self.root/'tokens.py'
        worker.write_text('''import array, fcntl, json, os, re, sys, termios
flags=os.environ.get('MAKEFLAGS','')
match=re.search(r'--jobserver-(?:fds|auth)=([^ ]+)',flags)
available=0
pipe=None
if match:
 value=match[1]
 opened=value.startswith('fifo:')
 descriptor=os.open(value[5:],os.O_RDONLY|os.O_NONBLOCK) if opened else int(value.split(',')[0])
 buffer=array.array('i',[0])
 fcntl.ioctl(descriptor,termios.FIONREAD,buffer,True)
 available=buffer[0]
 status=os.fstat(descriptor)
 pipe=[status.st_dev,status.st_ino]
 if opened: os.close(descriptor)
with open(os.environ['TOKEN_LOG'],'a') as log:
 log.write(json.dumps({'stage':sys.argv[1],'available':available,'pipe':pipe,'flags':flags})+'\\n')
''')
        child = self.root/'child.mk'
        command = shlex.join([sys.executable,str(worker)])
        child.write_text('token-child:\n\t+@'+command+' child\n')
        probe = self.root/'jobserver.mk'
        probe.write_text('token-parent:\n\t+@'+command+' parent\n'
            '\t+@$(MAKE) --no-print-directory -f '+str(child)+' token-child\n')
        return command, child, probe

    def parent_probe_command(self, probe):
        return ['make','--no-print-directory','-f','Makefile','-f',str(probe),'token-parent',
            'BACKENDS=simulated','ANCILLARY_GPIO=0','GUI_CLAMP=0','HOST_OS=Linux',
            'NPROCS=32','MEM_KB=16777216','MEM_AVAIL_KB=16777216','SWAP_KB=0',
            'GPIOD_CPP_HEADER_AVAILABLE=0','BUILD_METADATA_DIR='+str(self.root/'parent-generated'),
            'BUILD_METADATA_HEADER='+str(self.root/'parent-generated/build_metadata.hpp'),
            'BACKEND_CAPABILITIES_HEADER='+str(self.root/'parent-generated/backend_capabilities.hpp')]

    def test_actual_parent_and_recursive_jobserver_honor_explicit_budget(self):
        _, _, probe = self.jobserver_probe()
        base = self.parent_probe_command(probe)
        scenarios = (([],1,{}),(['-j2'],2,{}),(['-j1'],1,{}),([],2,{'MAKEFLAGS':'-j2'}))
        for options, jobs, inherited in scenarios:
            with self.subTest(options=options,inherited=inherited):
                log = self.root/'tokens'
                log.unlink(missing_ok=True)
                result = run_shell(shlex.join(base+options),cwd=ROOT/'src',
                    env=self.env|{'TOKEN_LOG':str(log)}|inherited,stage='actual-jobserver',watchdog=30)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                records = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual([r['stage'] for r in records],['parent','child'])
                self.assertEqual([r['available'] for r in records],[jobs-1,jobs-1],records)
                self.assertEqual(records[0]['pipe'],records[1]['pipe'],records)
                if jobs > 1: self.assertIsNotNone(records[0]['pipe'],records)

    def test_actual_components_preserve_parent_and_standalone_jobservers(self):
        command, child, probe = self.jobserver_probe()
        for component in ('MonitorFile','LCBLog','PPM-Manager','Singleton','INI-Handler',
                          'Signal-Handler','Mailbox','WSPR-Transmitter'):
            with self.subTest(component=component):
                directory = ROOT/'src'/component/'src'
                probe.write_text('token-parent:\n\t+@'+command+' parent\n'
                    '\t+@$(MAKE) --no-print-directory -C '+shlex.quote(str(directory))+
                    ' -f Makefile -f '+str(child)+' token-child\n')
                log = self.root/'tokens'
                log.unlink(missing_ok=True)
                result = run_shell(shlex.join(self.parent_probe_command(probe)+['-j2','SUDO=']),
                    cwd=ROOT/'src',env=self.env|{'TOKEN_LOG':str(log),'WSPRRYPI_DISABLE_HARDWARE_ACCESS':'1'},
                    stage='component-inherited-jobserver-'+component,watchdog=30)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                records = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual([r['available'] for r in records],[1,1],records)
                self.assertEqual(records[0]['pipe'],records[1]['pipe'],records)
                self.assertIsNotNone(records[0]['pipe'],records)
                for options, available in (([],0),(['-j2'],1)):
                    log.unlink()
                    result = run_shell(shlex.join(['make','--no-print-directory','-f','Makefile',
                        '-f',str(child),'token-child','BACKENDS=simulated','ANCILLARY_GPIO=0','SUDO=',*options]),
                        cwd=directory,env=self.env|{'TOKEN_LOG':str(log),'WSPRRYPI_DISABLE_HARDWARE_ACCESS':'1'},
                        stage='component-standalone-jobserver-'+component,watchdog=30)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    records = [json.loads(line) for line in log.read_text().splitlines()]
                    self.assertEqual([r['available'] for r in records],[available],records)

    def test_gated_parallel_prerequisite_and_interruption_recovery(self):
        for interrupted in (False, True):
            self.passed('clean')
            ready, release = self.root/'ready', self.root/'release'
            for path in (ready, release):
                path.unlink(missing_ok=True)
                os.mkfifo(path)
            rfd, wfd = os.open(ready, os.O_RDWR), os.open(release, os.O_RDWR)
            child = subprocess.Popen(['make','-j2','wtp-network-tools-build'], cwd=self.src,
                env=self.env | {'GATE_SOURCE':'wtp_integration/tls.cpp','READY':str(ready),'RELEASE':str(release)},
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
            try:
                self.assertTrue(select.select([rfd],[],[],30)[0], 'stage=compile-entry; missing explicit acknowledgment')
                self.assertEqual(os.read(rfd,64), b'entered\n')
                self.assertFalse(list((self.src/'build').rglob('bin/wtp_tls_probe')))
                if interrupted:
                    os.killpg(child.pid,signal.SIGTERM)
                else:
                    os.write(wfd,b'x')
                out, err = child.communicate(timeout=30)
                self.assertEqual(child.returncode == 0, not interrupted, out+err)
            finally:
                try: os.killpg(child.pid,signal.SIGKILL)
                except ProcessLookupError: pass
                child.communicate(timeout=5)
                os.close(rfd)
                os.close(wfd)
            self.assertEqual(list((self.src/'build').rglob('*.tmp')), [])
            self.passed('-j2','wtp-network-tools-build')
            self.reset_log()
            self.passed('wtp-network-tools-build')
            self.assertEqual(self.commands(), [])


if __name__ == '__main__':
    unittest.main()
