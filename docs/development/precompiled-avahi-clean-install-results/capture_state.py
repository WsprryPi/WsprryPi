"""Capture installed state without changing configuration or requesting RF."""
import argparse
import configparser
import hashlib
import json
import platform
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--phase', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
record = {'phase': args.phase, 'captured_utc_ns': str(time.time_ns()),
          'hostname': platform.node(), 'kernel': platform.release(),
          'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip()}
record['machine_id_sha256'] = hashlib.sha256(Path('/etc/machine-id').read_bytes()).hexdigest()
commands = {
    'service': ['systemctl', 'show', 'wsprrypi', '--property=LoadState,ActiveState,SubState,MainPID,ExecMainStatus,User,ExecStart'],
    'service_enabled': ['systemctl', 'is-enabled', 'wsprrypi'],
    'service_unit': ['systemctl', 'cat', 'wsprrypi'],
    'binary_version': ['runuser', '-u', 'pi', '--', '/usr/local/bin/wsprrypi', '--version'],
    'runtime_resolution': ['runuser', '-u', 'pi', '--', 'ldd', '-v', '/usr/local/bin/wsprrypi'],
    'apache_config': ['apache2ctl', 'configtest'],
    'apache_service': ['systemctl', 'is-active', 'apache2'],
    'avahi_service': ['systemctl', 'is-active', 'avahi-daemon'],
    'chrony_service': ['systemctl', 'is-active', 'chrony'],
    'addresses': ['ip', '-brief', 'address'],
    'routes': ['ip', 'route'],
    'recent_service_log': ['journalctl', '-u', 'wsprrypi', '-n', '60', '--no-pager', '-o', 'cat'],
}
for name, argv in commands.items():
    result = subprocess.run(argv, text=True, capture_output=True, timeout=25)
    record[name] = {'exit_status': result.returncode,
                    'stdout': result.stdout, 'stderr': result.stderr}
record['files'] = {}
for name in ['/usr/local/bin/wsprrypi', '/usr/local/etc/wsprrypi.ini',
             '/usr/local/etc/wsprrypi.ini.stock', '/etc/systemd/system/wsprrypi.service',
             '/home/pi/finished', '/root/.wsprrypi-wtp/revocation-v1']:
    path = Path(name)
    if path.exists():
        stat = path.stat()
        record['files'][name] = {'size': stat.st_size, 'mode': oct(stat.st_mode & 0o7777),
                                'uid': stat.st_uid, 'gid': stat.st_gid,
                                'mtime_ns': str(stat.st_mtime_ns),
                                'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    else:
        record['files'][name] = {'absent': True}
ini = configparser.ConfigParser(interpolation=None, strict=False)
ini.optionxform = str
ini.read('/usr/local/etc/wsprrypi.ini')
record['configuration'] = {section: dict(ini[section]) for section in ini.sections()}
result = subprocess.run(['dpkg-query', '-W', '-f=${binary:Package}\t${Version}\t${Status}\n'],
                        text=True, capture_output=True, check=True)
record['packages'] = {}
for line in result.stdout.splitlines():
    name, version, status = line.split('\t', 2)
    if status == 'install ok installed':
        record['packages'][name] = version
record['web_files'] = {}
web = Path('/var/www/html/wsprrypi')
for path in sorted(web.rglob('*')):
    if path.is_file() and not path.is_symlink():
        record['web_files'][str(path.relative_to(web))] = hashlib.sha256(path.read_bytes()).hexdigest()
Path(args.output).write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({'phase': args.phase, 'service': record['service'],
                  'binary_version': record['binary_version'],
                  'configuration': {s: record['configuration'][s] for s in ['Operation', 'WTP Server']}}))
