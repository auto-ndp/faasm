# Import build results
FROM kubasz51/faasm-base:0.8.2 AS builder

# Note - we don't often rebuild faabric-base so this dep may be behind
FROM kubasz51/faasm-faabric-base-runtime:0.3.1
ARG FAASM_VERSION

RUN apt-get update \
    && apt-get upgrade --yes --no-install-recommends \
    && apt-get install --yes --no-install-recommends \
    ansible \
    cgroup-tools \
    iproute2 \
    iptables \
    nasm \
    libcgroup1 \
    && apt-get clean autoclean \
    && apt-get autoremove

# Flag to say we're in a container
ENV FAASM_DOCKER="on"

# Copy Python runtime libraries
COPY --from=builder /usr/local/faasm/runtime_root /usr/local/faasm/runtime_root

# Set up SGX SDK
COPY --from=builder /opt/intel /opt/intel

# Check out code (clean beforehand just in case)
WORKDIR /usr/local/code
RUN rm -rf faasm
RUN git clone \
    --depth 1 \
    -b v${FAASM_VERSION} \
    https://github.com/auto-ndp/faasm
WORKDIR /usr/local/code/faasm

RUN git submodule update --init --depth 1

# Set up runtime filesystem
WORKDIR /usr/local/code/faasm/ansible
ENV USER=root
RUN ansible-playbook runtime_fs.yml

# Out of tree build
WORKDIR /build/faasm

RUN mkdir -p /build/faasm/bin
RUN mkdir -p /build/faasm/lib

COPY --from=builder /build/faasm/lib/*.so /build/faasm/lib/
