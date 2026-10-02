"""Read-only Pico JobService state via USB INFO, independent of LAN."""
import fcntl
import json
import os
import select
import struct
import sys
import termios
import threading
import time

class SerialStream:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = 0; attrs[1] = 0; attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; attrs[3] = 0
        attrs[6][termios.VMIN] = 0; attrs[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        fcntl.ioctl(self.fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR))
        time.sleep(.25)
        termios.tcflush(self.fd, termios.TCIFLUSH)
        fcntl.ioctl(self.fd, termios.TIOCMBIS, struct.pack('I', termios.TIOCM_DTR))
        time.sleep(.25)
    def recv(self, count):
        if not select.select([self.fd], [], [], 8)[0]:
            raise TimeoutError('USB WTP read timeout')
        return os.read(self.fd, count)
    def sendall(self, data):
        deadline = time.monotonic() + 8
        while data:
            if time.monotonic() > deadline:
                raise TimeoutError('USB WTP write timeout')
            if select.select([], [self.fd], [], .1)[1]:
                data = data[os.write(self.fd, data):]
    def close(self):
        fcntl.ioctl(self.fd, termios.TIOCMBIC, struct.pack('I', termios.TIOCM_DTR))
        os.close(self.fd)

output_lock = threading.Lock()
failed = threading.Event()
def emit(row):
    with output_lock:
        print(json.dumps(row), flush=True)
def observe(name, serial, expected, deadline, once):
    try:
        return observe_console(name, serial, expected, deadline, once)
    except Exception as error:
        failed.set()
        emit({'name':name, 'utc_ns':str(time.time_ns()), 'error':repr(error)})
        return

def observe_console(name, serial, expected, deadline, once):
    # CDC0 INFO embeds the shared JobService's actual state/output, but
    # does not expose ownership
    # or remote job IDs. Keep those fields absent; use Fleet's fresh protocol
    # evidence for them, and an independent LAN probe after final shutdown.
    stream = SerialStream('/dev/serial/by-id/usb-WsprryPi_WsprryPico_' + serial + '-if00')
    def command(text):
        stream.sendall(text.encode() + b'\n')
        buffer = b''
        end = time.monotonic() + 8
        while time.monotonic() < end:
            buffer += stream.recv(4096)
            # Only newline-terminated JSON is a complete console response.
            # A chunk can end at an inner object's closing brace.
            for line in buffer.split(b'\n')[:-1]:
                if line.startswith(b'{') and line.endswith(b'}'):
                    return json.loads(line)
        raise TimeoutError('Console status timeout')
    try:
        info = command('INFO')
        assert info['device_id'] == expected
        hello = {'device_id':expected, 'boot_id':info['status']['boot_id'],
                 'product':'WsprryPico', 'firmware_version':info['firmware'], 'revision':info['revision']}
        while time.time() < deadline:
            # This retained image's bare STATUS belongs to the clock console.
            # INFO embeds the shared JobService state under its status field.
            info = command('INFO')
            assert info['device_id'] == expected
            status = info['status']
            emit({'name':name, 'utc_ns':str(time.time_ns()), 'transport':'usb-console-read-only',
                  'hello':hello, 'caps':{'engine':status['engine']}, 'status':status,
                  'owner_and_remote_job_id_observed':False, 'events':[]})
            if once:
                return
            time.sleep(.5)
    finally:
        stream.close()

if __name__ == '__main__':
    once = sys.argv[1] == 'once'
    deadline = time.time() + 10 if once else int(sys.argv[1])
    threads = []
    for args in [('Pico A','0BF4B4AEC9FFB344','fd6127d11d6aca42a9905fa3fb1bf1d5'),
                 ('Pico B','CDDBF8767C506C07','29f20b7342051ef947aa56cb9d4fab42')]:
        thread = threading.Thread(target=observe, args=(*args, deadline, once))
        thread.start(); threads.append(thread)
    for thread in threads:
        thread.join()
    if failed.is_set():
        raise SystemExit(1)
