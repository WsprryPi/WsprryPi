# syntax=docker/dockerfile:1.7
FROM debian:bookworm-slim@sha256:7b140f374b289a7c2befc338f42ebe6441b7ea838a042bbd5acbfca6ec875818 AS build
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential binutils ca-certificates file git libavahi-client-dev libgpiod-dev \
        libssl-dev libsystemd-dev pkg-config python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .
RUN sh containers/build-artifact.sh aarch64 bookworm

FROM scratch AS artifact
COPY --from=build /artifact/ /
