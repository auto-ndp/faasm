FROM alannair/faasm-base-runtime:latest

RUN apt-get update --yes \
    && apt-get install --yes --no-install-recommends \
    git \
    libunwind-dev \
    python3-sphinx \
    clang-13 \
    clang-format-13 \
    clang-tidy-13 \
    clang-tools-13 \
    cmake 

RUN git clone --recursive https://github.com/ceph/ceph.git /usr/local/code/faasm/ceph \
    && cd /usr/local/code/faasm/ceph \
    && ./install-deps.sh

# Build the upload and codegen targets
WORKDIR /build/faasm

# Install hoststats
# RUN pip3 install hoststats==0.1.0

# Set up entrypoint
COPY bin/entrypoint_codegen.sh /entrypoint_codegen.sh
COPY bin/entrypoint_upload.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["/build/faasm/bin/upload"]
