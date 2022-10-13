FROM kubasz51/faasm-faabric-base-runtime:0.4.1

RUN apt-get update \
    && apt-get upgrade --yes --no-install-recommends \
    && apt-get install --yes --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ceph-common \
    clang-13 \
    clang-format-13 \
    clang-tidy-13 \
    clang-tools-13 \
    cmake \
    doxygen \
    g++-12 \
    git \
    libboost-filesystem-dev \
    libc++-13-dev \
    libc++abi-13-dev \
    libcurl4-openssl-dev \
    libhiredis-dev \
    liblttng-ust-dev \
    lttng-tools \
    libpython3-dev \
    librados-dev \
    libssl-dev \
    libstdc++-11-dev \
    libtool \
    libunwind-13-dev \
    zlib1g-dev \
    lld-13 \
    lldb-13 \
    llvm-11-dev \
    make \
    ninja-build \
    && apt-get clean autoclean --yes \
    && apt-get autoremove --yes

# Update pip
RUN pip install -U pip wheel setuptools

RUN pip install cmake==3.24.1 conan==1.53.0
