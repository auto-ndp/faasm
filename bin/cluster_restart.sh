#!/bin/bash

set -e

THIS_DIR=$(dirname $(readlink -f $0))
PROJ_ROOT=${THIS_DIR}/..

pushd ${PROJ_ROOT} > /dev/null

source ./bin/cluster_env.sh

echo FAASM_BUILD_MOUNT: $FAASM_BUILD_MOUNT

if [[ "$1" == "--rebuild" ]]; then
  docker-compose -f docker-compose.yml exec faasm-cli /bin/bash -c '. /usr/local/code/faasm/bin/cluster_env.sh; inv -r faasmcli/faasmcli dev.cc faasm_dev_tools -p $(($(nproc) - 2))'
fi

docker-compose -f docker-compose.yml exec redis-queue redis-cli flushall

docker-compose -f docker-compose.yml exec redis-state redis-cli flushall

docker-compose -f docker-compose.yml stop --timeout 1 worker worker-storage
docker-compose -f docker-compose.yml \
    up \
    --force-recreate --no-deps \
    --timeout 1 \
    -d \
    worker worker-storage

sleep 0.1

docker-compose -f docker-compose.yml \
    restart \
    --timeout 1 \
    nginx

popd > /dev/null
