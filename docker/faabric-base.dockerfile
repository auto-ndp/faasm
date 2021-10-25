FROM ubuntu:20.04

RUN apt-get update
RUN apt-get install -y software-properties-common gpg wget curl
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|apt-key add -
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc | apt-key add -
RUN add-apt-repository "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-13 main"
RUN add-apt-repository "deb https://apt.kitware.com/ubuntu/ focal main"
RUN add-apt-repository ppa:ubuntu-toolchain-r/test
RUN apt-get update

RUN apt install -y \
    ansible \
    autoconf \
    automake \
    build-essential \
    cgroup-tools \
    clang-13 \
    clang-format-13 \
    clang-tidy-13 \
    clang-tools-13 \
    cmake \
    gdb \
    git \
    iproute2 \
    iptables \
    kitware-archive-keyring \
    libboost-filesystem-dev \
    libc++-13-dev \
    libc++abi-13-dev \
    libcgroup-dev \
    libcurl4-openssl-dev \
    libpython3-dev \
    libstdc++-11-dev \
    g++-11 \
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
    wget \
    zsh

RUN curl -s -L -o /tmp/conan-latest.deb https://github.com/conan-io/conan/releases/latest/download/conan-ubuntu-64.deb && sudo dpkg -i /tmp/conan-latest.deb && rm -f /tmp/conan-latest.deb

# Tidy up
RUN apt-get clean autoclean
RUN apt-get autoremove
