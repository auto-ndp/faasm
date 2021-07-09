#!/bin/bash

set -e

THIS_DIR=$(dirname $(readlink -f $0))
PROJ_ROOT=${THIS_DIR}/..

pushd ${PROJ_ROOT} > /dev/null

source ./bin/cluster_env.sh

docker-compose -f docker-compose.yml \
    up \
    --force-recreate \
    --timeout 1 \
    -d \
    worker worker-storage

sleep 0.1

docker-compose -f docker-compose.yml \
    restart \
    --timeout 1 \
    nginx

popd > /dev/null
