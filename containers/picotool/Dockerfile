# syntax=docker/dockerfile:1.7
ARG BASE_IMAGE
FROM ${BASE_IMAGE} AS build
ARG DEBIAN_FRONTEND=noninteractive
ARG PICOTOOL_TARGET
# Match WsprryPi's Raspbian ARMv6 Trixie bootstrap, using signed repositories.
RUN if [ "$PICOTOOL_TARGET" = armv6-trixie ]; then \
      printf '%s\n' 'deb http://archive.raspbian.org/raspbian trixie main contrib non-free rpi firmware' > /etc/apt/sources.list; \
      apt-get update && apt-get -y dist-upgrade; \
    fi
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential binutils ca-certificates cmake git libusb-1.0-0-dev pkg-config python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /builder
COPY scripts/build_picotool.py scripts/picotool_install.py scripts/precompiled_binary.py \
     scripts/container_build.py scripts/generate_build_metadata.py ./
RUN python3 -B build_picotool.py --inside "$PICOTOOL_TARGET" /artifact
