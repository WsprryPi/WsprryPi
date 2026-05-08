#!/usr/bin/env bash

set -euo pipefail

WSPRRYP_PI_DIR="${HOME}/WsprryPi"
SRC_DIR="${WSPRRYP_PI_DIR}/src"
COPY_UI="${WSPRRYP_PI_DIR}/scripts/copy_ui.py"

start_time="$(date +%s)"

clear

cd "$SRC_DIR"

make clean
make release
make install
sudo "$COPY_UI"

end_time="$(date +%s)"
elapsed="$((end_time - start_time))"

printf "%s\n" "Build, install, and UI copy complete"
printf "Elapsed time: %02d:%02d:%02d\n" \
    "$((elapsed / 3600))" \
    "$(((elapsed % 3600) / 60))" \
    "$((elapsed % 60))"
