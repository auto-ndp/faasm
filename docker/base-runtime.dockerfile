ARG FAASM_VERSION
# Import build results
FROM kubasz51/faasm-base:${FAASM_VERSION} AS builder

# Note - we don't often rebuild faabric-base so this dep may be behind
FROM kubasz51/faasm-faabric-base-runtime:0.4.1
ARG FAASM_VERSION

RUN apt-get update \
    && apt-get upgrade --yes --no-install-recommends \
    && apt-get install --yes --no-install-recommends \
    ceph-common \
    cgroup-tools \
    iproute2 \
    iptables \
    nasm \
    libcgroup1 \
    librados2 \
    && apt-get clean autoclean --yes \
    && apt-get autoremove --yes

# Flag to say we're in a container
ENV FAASM_DOCKER="on"

# Copy Python runtime libraries
COPY --from=builder /usr/local/faasm/runtime_root /usr/local/faasm/runtime_root

# Check out code (clean beforehand just in case)
WORKDIR /usr/local/code
RUN rm -rf faasm
COPY --from=builder /usr/local/code/faasm /usr/local/code/faasm

# Set up runtime filesystem
RUN mkdir -p /usr/local/faasm/runtime_root/etc \
    && cp /usr/local/code/faasm/deploy/conf/hosts /usr/local/faasm/runtime_root/etc/ \
    && cp /usr/local/code/faasm/deploy/conf/resolv.conf /usr/local/faasm/runtime_root/etc/ \
    && cp /usr/local/code/faasm/deploy/conf/passwd /usr/local/faasm/runtime_root/etc/ \
    && mkdir -p /usr/local/faasm/runtime_root/tmp \
    && mkdir -p /usr/local/faasm/runtime_root/share

# Out of tree build
WORKDIR /build/faasm

RUN mkdir -p /build/faasm/bin
RUN mkdir -p /build/faasm/lib

COPY --from=builder /build/faasm/lib/*.so /build/faasm/lib/
COPY --from=builder /build/faasm/bin/func_runner /build/faasm/bin/func_sym \
    /build/faasm/bin/codegen_func /build/faasm/bin/codegen_shared_obj  \
    /build/faasm/bin/pool_runner /build/faasm/bin/upload /build/faasm/bin/
