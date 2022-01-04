#!/bin/bash

set -e

# Run codegen
THIS_DIR=$(dirname $(readlink -f $0))
$THIS_DIR/entrypoint_codegen.sh

# Start hoststats
# nohup hoststats start > /var/log/hoststats.log 2>&1 &

# Set up isolation
pushd /usr/local/code/faasm >> /dev/null

echo "Setting up cgroup"
./bin/cgroup.sh

echo "Set stack size"
ulimit -s 16384

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
