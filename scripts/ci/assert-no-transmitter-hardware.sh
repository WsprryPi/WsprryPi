#!/bin/sh
set -eu

if [ "$#" -eq 0 ]; then
    echo "usage: $0 TRACE_FILE..." >&2
    exit 2
fi

pattern='/dev/(gpiomem|mem|vcio|i2c-[^" ]*|gpiochip[^" ]*|rp1-gpclk[^" ]*)|/sys/class/gpio|/sys/kernel/debug|/dev/mailbox|/dev/dma'

for trace_file in "$@"; do
    if [ ! -f "$trace_file" ] || [ ! -s "$trace_file" ] || [ ! -r "$trace_file" ]; then
        echo "Missing, empty or unreadable hardware-access trace: $trace_file" >&2
        exit 2
    fi
done

if grep -En -- "$pattern" "$@"; then
    echo "Prohibited transmitter hardware access was observed." >&2
    exit 1
else
    grep_status=$?
    if [ "$grep_status" -ne 1 ]; then
        echo "Unable to inspect hardware-access traces." >&2
        exit 2
    fi
fi

echo "No prohibited transmitter hardware access observed."
