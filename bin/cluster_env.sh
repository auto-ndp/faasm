
THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
PROJ_ROOT=${THIS_DIR}/..

set -o allexport
source ${PROJ_ROOT}/.env
if [ -f ${PROJ_ROOT}/.exptcfg ]; then
  source ${PROJ_ROOT}/.exptcfg
fi
set +o allexport

# Mount our local build into the local cluster
export FAASM_ROOT=${PROJ_ROOT}
export FAASM_VERSION=$(cat ${PROJ_ROOT}/VERSION)
export FAASM_BUILD_MOUNT=/build/faasm
export FAASM_LOCAL_MOUNT=/usr/local/faasm

touch /tmp/faasm-monitor
touch /tmp/faasm-monitor-storage

export INNER_SHELL=/bin/bash
