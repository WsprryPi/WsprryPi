#!/bin/bash
set -eu
stage=$1
binary=/home/pi/wtp-caps-validation.wglNdL/src/build/bin/wsprrypi_debug
test -x "$binary"
cd "$stage"
cp /usr/local/etc/wsprrypi.ini validation.ini
sha256sum /usr/local/etc/wsprrypi.ini > installed-ini-before.sha256
systemctl show wsprrypi.service -p ActiveState -p ExecMainStatus -p MainPID > service-before.txt
python3 - "$stage/validation.ini" <<'PY'
import configparser,sys,time
p=sys.argv[1]
c=configparser.ConfigParser(strict=False,interpolation=None);c.optionxform=str;c.read(p)
c['Operation'].update({'Transmit':'false','Transmit Backend':'si5351','Enable on Boot':'Never',
    'Use LED':'false','Use Amp':'false','Use Shutdown':'false','Mode':'QRSS'})
c['WTP Server']={'Enabled':'true','Port':'31418','Interface':'wlan0'}
if 'Experimental' not in c:c['Experimental']={}
c['Experimental']['Allow Unqualified Frequency']='true'
if 'Band GPIO' in c:c.remove_section('Band GPIO')
if 'CW' not in c:c['CW']={}
c['CW'].update({'Message':'E','Base Frequency':'14097100','Dot Seconds':'3',
    'Start Minute':str((time.gmtime().tm_min+30)%60),'Start Second':'0','Repeat Minutes':'60'})
with open(p,'w') as f:c.write(f)
PY
chmod 600 validation.ini
service_was_active=$(systemctl is-active wsprrypi.service || true)
pid=
cleanup() {
    trap - EXIT INT TERM HUP
    if [ -n "$pid" ]; then kill -INT "$pid" 2>/dev/null || true; wait "$pid" || true; fi
    rm -f active-test.pid
    if [ "$service_was_active" = active ]; then systemctl start wsprrypi.service; fi
    systemctl show wsprrypi.service -p ActiveState -p ExecMainStatus -p MainPID > service-after.txt
    sha256sum /usr/local/etc/wsprrypi.ini > installed-ini-after.sha256
    touch restored
}
trap cleanup EXIT INT TERM HUP
systemctl stop wsprrypi.service
timeout --signal=INT --kill-after=10 300 "$binary" -D -i "$stage/validation.ini" \
    --backend si5351 --wtp-server-port 31418 --allow-unqualified-frequency > runtime.log 2>&1 &
pid=$!
echo "$pid" > active-test.pid
while kill -0 "$pid" 2>/dev/null && [ ! -e stop-test ]; do sleep 1; done
