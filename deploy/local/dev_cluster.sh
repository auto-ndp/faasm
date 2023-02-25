#!/bin/bash
set -e

THIS_DIR=$(dirname $(readlink -f $0))
PROJ_ROOT=${THIS_DIR}/../..
pushd ${PROJ_ROOT} > /dev/null

COMPOSE_FILE=${1:-docker-compose.yml} 

export FAASM_BUILD_MOUNT=/build/faasm
export FAASM_LOCAL_MOUNT=/usr/local/faasm

docker compose -f ${COMPOSE_FILE} \
    up \
    -d \
    worker worker-storage upload nginx

popd > /dev/null
