#!/bin/sh
set -eu

if [ "$#" -eq 0 ]; then
    echo "usage: $0 TRACE_FILE..." >&2
    exit 2
fi

device='(gpiomem[^/" >]*|mem|vcio[0-9]*|i2c-[^/" >]*|gpiochip[^/" >]*|rp1-gpclk[^/" >]*|mailbox[^/" >]*|dma[^/" >]*)'
device_path="dev/$device"
sysfs_path='sys/(class/gpio|kernel/debug)'
# strace -y includes the base directory for relative openat calls and the
# resolved path of any descriptor the kernel actually opened.
boundary='([/" >]|$)'
pattern="/($device_path|$sysfs_path)$boundary"
pattern="$pattern|</>,[[:space:]]*\"(\./)*($device_path|$sysfs_path)$boundary"
pattern="$pattern|</dev>,[[:space:]]*\"(\./)*$device$boundary"
pattern="$pattern|</sys>,[[:space:]]*\"(\./)*(class/gpio|kernel/debug)$boundary"
pattern="$pattern|</sys/class>,[[:space:]]*\"(\./)*gpio$boundary"
pattern="$pattern|</sys/kernel>,[[:space:]]*\"(\./)*debug$boundary"

status=0
for trace_file in "$@"; do
    if [ ! -f "$trace_file" ] || [ ! -s "$trace_file" ] || [ ! -r "$trace_file" ]; then
        echo "Missing, empty or unreadable hardware-access trace: $trace_file" >&2
        status=2
    elif grep -EnH -- "$pattern" "$trace_file"; then
        echo "Prohibited transmitter hardware access was observed." >&2
        if [ "$status" -eq 0 ]; then
            status=1
        fi
    elif [ "$?" -ne 1 ]; then
        echo "Unable to inspect hardware-access trace: $trace_file" >&2
        status=2
    fi
done

if [ "$status" -ne 0 ]; then
    exit "$status"
fi

echo "No prohibited transmitter hardware access observed."
