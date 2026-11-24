#!/bin/bash
set -euo pipefail
# Root execution needs separate explicit qualification authority. --prepare-only
# uses an unprivileged read-only container and prepares reviewable fixture inputs.
if [[ $# -lt 2 || $# -gt 3 || ( $# -eq 3 && $3 != --prepare-only && $3 != --crash-at=publishing_generation ) ]]; then
    printf 'Usage: %s <controller-native-accounts-fixture> <recovery-age> [--prepare-only|--crash-at=publishing_generation]\n' "$0" >&2
    exit 2
fi
repository=$(cd "$(dirname "$0")/../.." && pwd -P)
prepared=$(mktemp -d /tmp/wsprrypi-native-accounts-input.XXXXXXXX)
chmod 700 "$prepared"
cp "$1" "$prepared/controller_native_accounts_fixture"
cp "$2" "$prepared/recovery-age"
cp "$repository/scripts/tests/controller_native_accounts_test.py" "$prepared/controller_native_accounts_test.py"
cp -R "$repository/src/application_recovery" "$prepared/application_recovery"
docker run --rm --pull=never --platform linux/amd64 --network none --read-only --cap-drop ALL \
    issue475-controller-amd64-tests:20261122 python3 -I -c '
import json
from pathlib import Path
print(json.dumps({name:Path("/etc",name).read_text() for name in ("passwd","group")}))
' > "$prepared/base-accounts.json"
python3 -B - "$prepared" <<'PY'
import hashlib,json,secrets,sys
from pathlib import Path
root=Path(sys.argv[1]);base=json.loads((root/'base-accounts.json').read_text())
accounts=(('wsprrypi-controller',51001,'/var/lib/wsprrypi/controller'),
          ('wsprrypi-web',51002,'/var/lib/wsprrypi-web'),('www-data',51003,'/var/empty'))
for kind in ('passwd','group'):
 lines=[line for line in base[kind].splitlines() if line.split(':')[0]!='www-data']
 assert not any(line.split(':')[0] in ('wsprrypi-controller','wsprrypi-web') or int(line.split(':')[2]) in (51001,51002,51003) for line in lines if line and not line.startswith('#'))
 for name,uid,home in accounts:
  lines.append(f'{name}:x:{uid}:{uid}:Private qualification:{home}:/usr/sbin/nologin' if kind=='passwd' else f'{name}:x:{uid}:')
 (root/kind).write_text('\n'.join(lines)+'\n');(root/kind).chmod(0o644)
(root/'machine-id').write_text(secrets.token_hex(16)+'\n');(root/'machine-id').chmod(0o644)
# The host bundle remains 0700. Individually mounted code inputs must be
# readable without granting root DAC override inside the qualification box.
for path in root.rglob('*'):
 assert not path.is_symlink()
 path.chmod(0o755 if path.is_dir() else 0o644)
for name in ('controller_native_accounts_fixture','recovery-age'):
 (root/name).chmod(0o755)
manifest={}
for path in root.rglob('*'):
 if path.is_file() and '__pycache__' not in path.parts:
  manifest[path.relative_to(root).as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
(root/'source-manifest.json').write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
PY
printf 'Prepared private qualification inputs: %s\n' "$prepared"
if [[ ${3:-} == --prepare-only ]]; then
    exit 0
fi
qualification_arguments=(--isolated-container)
if [[ ${3:-} == --crash-at=publishing_generation ]]; then
    qualification_arguments+=(--crash-at=publishing_generation)
fi
docker run --rm --pull=never --init --platform linux/amd64 --user 0:0 --workdir /tmp \
    --network none --read-only --pids-limit 128 --memory 768m --cpus 2 \
    --cap-drop ALL --cap-add CHOWN --cap-add SETUID --cap-add SETGID --cap-add KILL \
    --security-opt no-new-privileges:true \
    --mount "type=bind,src=$prepared/controller_native_accounts_fixture,dst=/source/controller_native_accounts_fixture,readonly" \
    --mount "type=bind,src=$prepared/recovery-age,dst=/source/recovery-age,readonly" \
    --mount "type=bind,src=$prepared/controller_native_accounts_test.py,dst=/source/controller_native_accounts_test.py,readonly" \
    --mount "type=bind,src=$prepared/application_recovery,dst=/source/application_recovery,readonly" \
    --mount "type=bind,src=$prepared/passwd,dst=/etc/passwd,readonly" \
    --mount "type=bind,src=$prepared/group,dst=/etc/group,readonly" \
    --mount "type=bind,src=$prepared/machine-id,dst=/etc/machine-id,readonly" \
    --tmpfs /tmp:rw,exec,mode=1777 --tmpfs /var/lib:rw,mode=755 \
    --tmpfs /run:rw,mode=755 --tmpfs /home:rw,mode=755 \
    issue475-controller-amd64-tests:20261122 \
    timeout --kill-after=5s 240s python3 -I /source/controller_native_accounts_test.py "${qualification_arguments[@]}"
