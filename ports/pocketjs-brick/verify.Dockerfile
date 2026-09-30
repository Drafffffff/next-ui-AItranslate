# Reference SDL driver for CI smoke checks, separate from the device sysroot.
FROM ghcr.io/loveretro/tg5040-toolchain@sha256:f131c6af64029a8723d0ce8d3c2682642f5f091b04714f6beedda9bec18477ab
RUN apt-get update && apt-get install -y --no-install-recommends libsdl2-2.0-0 libsdl2-ttf-2.0-0 curl openssl && rm -rf /var/lib/apt/lists/*
