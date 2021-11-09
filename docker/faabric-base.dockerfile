FROM ubuntu:20.04

RUN apt-get update
RUN apt-get install -y software-properties-common gpg wget curl
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|apt-key add -
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | apt-key add -
RUN add-apt-repository -y -n "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-10 main"
RUN add-apt-repository -y -n "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-13 main"
RUN add-apt-repository -y -n "deb https://apt.kitware.com/ubuntu/ focal main"
RUN add-apt-repository -y -n ppa:ubuntu-toolchain-r/test
RUN apt-get update

RUN apt install -y \
    autoconf \
    automake \
    build-essential \
    clang-10 \
    clang-13 \
    clang-format-10 \
    clang-format-13 \
    clang-tidy-10 \
    clang-tidy-13 \
    clang-tools-13 \
    cmake \
    g++-11 \
    gdb \
    git \
    kitware-archive-keyring \
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

RUN pip install conan

# Tidy up
RUN apt-get clean autoclean
RUN apt-get autoremove
