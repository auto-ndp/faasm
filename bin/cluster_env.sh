
THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
PROJ_ROOT=${THIS_DIR}/..

export FAASM_VERSION=$(cat ${PROJ_ROOT}/VERSION)
export CPP_VERSION=$(cat ${PROJ_ROOT}/clients/cpp/VERSION)
export PYTHON_VERSION=$(cat clients/python/VERSION)

# Mount our local build into the local cluster
export FAASM_BUILD_MOUNT=/build/faasm
export FAASM_LOCAL_MOUNT=/usr/local/faasm

if [[ -z "$FAASM_CLI_IMAGE" ]]; then
    export FAASM_CLI_IMAGE=kubasz51/faasm-cli:${FAASM_VERSION}
fi

if [[ -z "$CPP_CLI_IMAGE" ]]; then
    export CPP_CLI_IMAGE=kubasz51/faasm-cpp-sysroot:${CPP_VERSION}
fi

if [[ -z "$PYTHON_CLI_IMAGE" ]]; then
    export PYTHON_CLI_IMAGE=kubasz51/faasm-cpython:${PYTHON_VERSION}
fi

export INNER_SHELL=/bin/bash
