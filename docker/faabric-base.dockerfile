FROM debian:bookworm-20211115
# Debian "12" testing

RUN apt-get update
RUN apt-get upgrade -y
RUN apt-get install -y software-properties-common gpg wget curl

RUN apt-get install -y \
    autoconf \
    automake \
    build-essential \
    clang-11 \
    clang-13 \
    clang-format-11 \
    clang-format-13 \
    clang-tidy-11 \
    clang-tidy-13 \
    clang-tools-13 \
    cmake \
    g++-11 \
    gdb \
    git \
    libboost-filesystem-dev \
    libc++-13-dev \
    libc++abi-13-dev \
    libcurl4-openssl-dev \
    libhiredis-dev \
    libpython3-dev \
    libssl-dev \
    libstdc++-11-dev \
    libtool \
    libunwind-13-dev \
    libz-dev \
    lld-13 \
    lldb-13 \
    make \
    ninja-build \
    pkg-config \
    python3-dev \
    python3-pip \
    python3-venv \
    redis-tools \
    sudo \
    unzip \
    zsh

RUN pip install cmake
RUN pip install conan

# Tidy up
RUN apt-get clean autoclean
RUN apt-get autoremove
