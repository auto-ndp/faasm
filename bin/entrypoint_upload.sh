#!/bin/bash

set -e

if [[ -e "/ceph/build" ]]
then
    cd /ceph/build
    ninja install
fi

MON_CEPH_DIR="/usr/local/faasm/ceph-ceph-mon1"

while [[ ! -e "${MON_CEPH_DIR}/ceph.mon.keyring" ]]; do
    echo "Waiting for monitor keyring"
    sleep 1
done

cp -a "${MON_CEPH_DIR}"/ceph.client.admin.keyring /etc/ceph/ceph.client.admin.keyring

# Run codegen
THIS_DIR=$(dirname $(readlink -f $0))
$THIS_DIR/entrypoint_codegen.sh

# Start hoststats
# nohup hoststats start > /var/log/hoststats.log 2>&1 &

exec "$@"
