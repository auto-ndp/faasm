#!/bin/bash

set -e

THIS_DIR=$(dirname $(readlink -f $0))
PROJ_ROOT=${THIS_DIR}/..

pushd ${PROJ_ROOT} > /dev/null

source ./bin/cluster_env.sh

if [[ -z "$1" ]]; then
    CLI_CONTAINER="faasm-cli"
else
    CLI_CONTAINER="$1"
fi

docker-compose -f docker-compose.yml \
    up \
    --no-recreate \
    -d \
    faasm-cli cpp

docker-compose -f docker-compose.yml \
    exec \
    ${CLI_CONTAINER} \
    ${INNER_SHELL}

popd > /dev/null
