# FROM kubasz51/faasm-faabric-base:0.4.1
FROM alannair/faasm-faabric-base:latest
ARG FAASM_VERSION

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

RUN git clone https://github.com/auto-ndp/autondp-ceph.git /ceph \
    && cd /ceph \
	&& git submodule update --init --recursive \
	&& ./install-deps.sh \
    && cd .. && rm -rf ceph

SHELL [ "/bin/bash" ]
