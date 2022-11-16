#!/bin/bash

set -e

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

# Set up isolation
pushd /usr/local/code/faasm >> /dev/null

echo "Setting up cgroup"
./bin/cgroup.sh

echo "Setting up namespaces"
./bin/netns.sh ${MAX_NET_NAMESPACES}

popd >> /dev/null

# Continue with normal command
exec "$@"

# Comment the above to run an infinite loop and allow running with gdb
while true
do
    sleep 1;
done
