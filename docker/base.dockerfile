# Stage to copy Conan cache
FROM kubasz51/faasm-faabric:0.4.1 as faabric

# Stage to extract Python runtime files
FROM kubasz51/faasm-cpython:0.1.5 as python

# Note - we don't often rebuild faabric-base so this dep may be behind
FROM kubasz51/faasm-faabric-base:0.4.1
ARG FAASM_VERSION

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get upgrade --yes --no-install-recommends \
    && apt-get install --yes --no-install-recommends \
    ansible \
    cgroup-tools \
    iproute2 \
    iptables \
    nasm \
    libclang-common-11-dev \
    libcgroup-dev \
    && apt-get clean autoclean --yes \
    && apt-get autoremove --yes

# Flag to say we're in a container
ENV FAASM_DOCKER="on"

# Copy Conan cache
COPY --from=faabric /root/.conan /root/.conan

# Copy Python runtime libraries
COPY --from=python /usr/local/faasm/runtime_root /usr/local/faasm/runtime_root

# Check out code (clean beforehand just in case)
RUN rm -rf /usr/local/code/faasm \
    && git clone \
    	--depth 1 \
        -b v${FAASM_VERSION} \
    	https://github.com/auto-ndp/faasm \
        /usr/local/code/faasm \
    && cd /usr/local/code/faasm \
    && git submodule update --init

# Set up runtime filesystem
RUN mkdir -p /usr/local/faasm/runtime_root/etc \
    && cp /usr/local/code/faasm/deploy/conf/hosts /usr/local/faasm/runtime_root/etc/ \
    && cp /usr/local/code/faasm/deploy/conf/resolv.conf /usr/local/faasm/runtime_root/etc/ \
    && cp /usr/local/code/faasm/deploy/conf/passwd /usr/local/faasm/runtime_root/etc/ \
    && mkdir -p /usr/local/faasm/runtime_root/tmp \
    && mkdir -p /usr/local/faasm/runtime_root/share

# Out of tree clean build of the basic targets
RUN rm -rf /build/faasm \
    && mkdir -p /build/faasm \
    && cd /build/faasm \
    && cmake \
        -GNinja \
        -DCMAKE_CXX_COMPILER=/usr/bin/clang++-13 \
        -DCMAKE_C_COMPILER=/usr/bin/clang-13 \
        -DCMAKE_BUILD_TYPE=Release \
        -DFAASM_SGX_MODE=Disabled \
        /usr/local/code/faasm \
    && cmake --build . --target tests func_runner func_sym codegen_func codegen_shared_obj pool_runner upload
