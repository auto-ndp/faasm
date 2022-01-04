FROM debian:bookworm-20211220-slim
# Debian "12" testing

RUN apt-get update \
    && apt-get upgrade --yes --no-install-recommends \
    && apt-get install --yes --no-install-recommends software-properties-common gpg wget curl \
    && apt-get install --yes --no-install-recommends \
    gdb \
    gdbserver \
    libc++-13 \
    libc++abi-13 \
    libcurl4-openssl \
    libhiredis \
    libllvm11 \
    libllvm13 \
    liblttng-ust \
    lttng-tools \
    libpython3 \
    libssl \
    libstdc++-6 \
    libunwind-13 \
    libz \
    pkg-config \
    python3-dev \
    python3-pip \
    python3-venv \
    python3-wheel \
    redis-tools \
    sudo \
    unzip \
    && apt-get clean autoclean \
    && apt-get autoremove

