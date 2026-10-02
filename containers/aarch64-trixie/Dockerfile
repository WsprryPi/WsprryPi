# syntax=docker/dockerfile:1.7
FROM debian:trixie-slim@sha256:3a39a0592364683e6bab97937b72cad5a8fa6dcbbee90edb3bb48c7f8e94f258 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential binutils ca-certificates file git libavahi-client-dev libgpiod-dev \
        libssl-dev libsystemd-dev pkg-config python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .
RUN sh containers/build-artifact.sh aarch64 trixie

FROM scratch AS artifact
COPY --from=build /artifact/ /
