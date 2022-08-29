FROM kubasz51/faasm-faabric-base-runtime:0.3.1
# Debian "12" testing

RUN apt-get update \
    && apt-get upgrade --yes --no-install-recommends \
    && apt-get install --yes --no-install-recommends software-properties-common gpg wget curl \
    && apt-get install --yes --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    clang-13 \
    clang-format-13 \
    clang-tidy-13 \
    clang-tools-13 \
    cmake \
    doxygen \
    g++-11 \
    git \
    libboost-filesystem-dev \
    libc++-13-dev \
    libc++abi-13-dev \
    libcurl4-openssl-dev \
    libhiredis-dev \
    liblttng-ust-dev \
    lttng-tools \
    libpython3-dev \
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
    && apt-get clean autoclean \
    && apt-get autoremove

# Update pip
RUN pip install -U pip

RUN pip install cmake==3.22.1
RUN pip install conan==1.44.0
