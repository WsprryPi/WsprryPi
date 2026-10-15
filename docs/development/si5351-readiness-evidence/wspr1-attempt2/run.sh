#!/bin/bash
# One authorized I2C-only attempt; preserve installed files and service state.
set -euo pipefail
run_dir=/home/pi/si5351-readiness-20261015-item6-r2
cd "$run_dir"
exec > >(tee run.log) 2>&1
stopped_service=0
restore() {
    run_result=$?
    trap - EXIT INT TERM
    set +e
    if pgrep -f "^python3 -u $run_dir/diagnostic.py$" > surviving-process.txt; then
        echo 'RESTORATION BLOCKED: diagnostic still owns or may own I2C; no competing service restart.'
        run_result=2
    elif [ "$stopped_service" -eq 1 ]; then
        sudo -n systemctl start wsprrypi.service || run_result=2
    fi
    systemctl show wsprrypi.service -p ActiveState -p SubState -p MainPID > service-after.txt
    cat service-after.txt
    sha256sum -c installed-before.sha256 || run_result=2
    date -u +%FT%TZ
    echo "run_exit=$run_result"
    exit "$run_result"
}
test "$(hostname)" = wspr1
test "$(systemctl is-active wsprrypi.service)" = active
test "$(sha256sum /usr/local/bin/wsprrypi | cut -d ' ' -f 1)" = e56f8abe0db12175803f0563722503533c912c45fdd9860b4f667d6ee0f21599
test "$(sha256sum /usr/local/etc/wsprrypi.ini | cut -d ' ' -f 1)" = 9f659e3e217ebf48573a68d0fc7d06cac2d45b1c984e7c5adc23f9dd424bbad5
sha256sum /usr/local/bin/wsprrypi /usr/local/etc/wsprrypi.ini > installed-before.sha256
sha256sum diagnostic.py > diagnostic.sha256
systemctl show wsprrypi.service -p ActiveState -p SubState -p MainPID > service-before.txt
date -u +%FT%TZ
cat service-before.txt
# Arm restoration before the mutating service command.
trap restore EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
stopped_service=1
sudo -n systemctl stop wsprrypi.service
if systemctl is-active --quiet wsprrypi.service; then
    echo 'Daemon did not stop; diagnostic refused.'
    exit 2
fi
sudo -n python3 - <<'PY'
import os,pathlib
owners=[]
for process in pathlib.Path('/proc').iterdir():
    if not process.name.isdigit(): continue
    try:
        for fd in (process/'fd').iterdir():
            try:
                if os.readlink(fd)=='/dev/i2c-1': owners.append((process.name,fd.name))
            except OSError: pass
    except OSError: pass
print({'i2c1_open_descriptors_before':owners})
if owners: raise SystemExit('Competing I2C owner; diagnostic refused')
PY
set +e
timeout --foreground --signal=TERM --kill-after=5s 60s \
    python3 -u "$run_dir/diagnostic.py" > diagnostic.jsonl 2> diagnostic.stderr
run_result=$?
set -e
cat diagnostic.jsonl
cat diagnostic.stderr
exit "$run_result"
