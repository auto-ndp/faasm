FROM debian:bookworm-20220822-slim
# Debian "12" testing

RUN apt-get update \
    && apt-get upgrade --yes --no-install-recommends \
    && apt-get install --yes --no-install-recommends software-properties-common gpg wget curl \
    && apt-get install --yes --no-install-recommends \
    gdb \
    gdbserver \
    libc++1-13 \
    libcurl4 \
    libhiredis0.14 \
    libllvm11 \
    libllvm13 \
    liblttng-ust-ctl5 \
    liblttng-ust1 \
    libpython3-dev \
    libssl3 \
    libstdc++6 \
    libunwind-13 \
    liburcu8 \
    libz3-4 \
    lttng-tools \
    zlib1g \
    pkg-config \
    python3-dev \
    python3-pip \
    python3-venv \
    python3-wheel \
    redis-tools \
    sudo \
    unzip \
    && apt-get clean autoclean --yes \
    && apt-get autoremove --yes

