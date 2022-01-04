ARG FAASM_VERSION
FROM kubasz51/faasm-base-runtime:${FAASM_VERSION}

# Build the worker binary
WORKDIR /build/faasm

# Install hoststats
RUN pip3 install hoststats==0.1.0

# Set up entrypoint (for cgroups, namespaces etc.)
COPY bin/entrypoint_codegen.sh /entrypoint_codegen.sh
COPY bin/entrypoint_worker.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

# Create user with dummy uid required by Python
RUN groupadd -g 1000 faasm
RUN useradd -u 1000 -g 1000 faasm

ENTRYPOINT ["/entrypoint.sh"]
CMD ["/build/faasm/bin/pool_runner"]
