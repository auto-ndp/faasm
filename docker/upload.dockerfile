FROM kubasz51/faasm-base-runtime:0.9.1

RUN apt-get update --yes \
    && apt-get install --yes git libunwind-dev python3-sphinx

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
