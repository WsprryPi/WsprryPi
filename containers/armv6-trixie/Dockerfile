# syntax=docker/dockerfile:1.7
FROM balenalib/raspberry-pi:bookworm-build@sha256:5e44b1697cd34bac1eaf43fbadab44c54d25246ff0b57144b054b8039d85141a AS build
ARG DEBIAN_FRONTEND=noninteractive
# No Balena Trixie ARMv6 image is published. Upgrade the pinned Raspbian
# bootstrap using only the signed Raspbian Trixie repository, never Debian armhf.
RUN printf '%s\n' 'deb http://archive.raspbian.org/raspbian trixie main contrib non-free rpi firmware' > /etc/apt/sources.list \
    && apt-get update \
    && apt-get -y dist-upgrade

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential binutils ca-certificates file git libavahi-client-dev libgpiod-dev \
        libssl-dev libsystemd-dev pkg-config python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .
RUN sh containers/build-artifact.sh armv6 trixie

FROM scratch AS artifact
COPY --from=build /artifact/ /
