#!/usr/bin/env bash
set -u
set -o pipefail

log() {
    printf "%s\n" "$*" >&2
}
