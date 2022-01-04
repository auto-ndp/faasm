ARG FAASM_VERSION
FROM kubasz51/faasm-base-runtime:${FAASM_VERSION}

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
