
THIS_DIR=$(dirname $(readlink -f $0))
PROJ_ROOT=${THIS_DIR}/..

export FAASM_VERSION=$(cat ${PROJ_ROOT}/VERSION)
export CPP_VERSION=$(cat ${PROJ_ROOT}/clients/cpp/VERSION)

# Mount our local build into the local cluster
export FAASM_BUILD_MOUNT=/build/faasm

if [[ -z "$FAASM_CLI_IMAGE" ]]; then
    export FAASM_CLI_IMAGE=kubasz51/faasm-cli:${FAASM_VERSION}
fi

if [[ -z "$CPP_CLI_IMAGE" ]]; then
    export CPP_CLI_IMAGE=kubasz51/faasm-cpp-sysroot:${CPP_VERSION}
fi

export INNER_SHELL=/bin/bash
