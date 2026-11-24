#!/bin/bash
set -euo pipefail
# Explicit root qualification authority is required before invoking this file.
# Docker supplies fresh namespaces/tmpfs; the root test cannot mount host paths
# or change accounts, installed services, or host configuration.
repository=$(cd "$(dirname "$0")/../.." && pwd -P)
docker run --rm --pull=never --init --platform linux/amd64 --user 0:0 --workdir /tmp \
    --network none --read-only --pids-limit 64 --memory 256m --cpus 1 \
    --cap-drop ALL --cap-add SETUID --cap-add SETGID --cap-add KILL \
    --security-opt no-new-privileges:true \
    --mount "type=bind,src=$repository/src/application_recovery,dst=/source/application_recovery,readonly" \
    --mount "type=bind,src=$repository/scripts/tests/recovery_state_owner_privileged_test.py,dst=/source/recovery_state_owner_privileged_test.py,readonly" \
    --tmpfs /tmp:rw,exec,mode=1777 \
    --tmpfs /home/wsprry-test:rw,uid=501,gid=20,mode=700 \
    issue475-controller-amd64-tests:20261122 \
    timeout --kill-after=5s 90s python3 -I /source/recovery_state_owner_privileged_test.py --isolated-container
