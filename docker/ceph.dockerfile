FROM alannair/faasm-faabric-base:latest

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get upgrade --yes --no-install-recommends \
    && apt-get install --yes --no-install-recommends \
    ceph \
    ceph-mds \
    ceph-volume \
    openssh-server \
    tini \
    libunwind-dev \
    && apt-get clean autoclean --yes \
    && apt-get autoremove --yes

# Flag to say we're in a container
ENV FAASM_DOCKER="on"

COPY ./deploy/conf/ceph/ /etc/ceph/
COPY ./bin/run_ceph_*.sh /

RUN rm -rf /usr/local/code/faasm \
    && git clone https://github.com/auto-ndp/faasm /usr/local/code/faasm \
    && cd /usr/local/code/faasm \
    && git submodule update --init --recursive ceph \
    && cd ceph \
    && ./install-deps.sh

SHELL [ "/bin/bash" ]