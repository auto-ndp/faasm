#!/bin/bash

# cd /usr/local/code/faasm/ceph
# apt-get update -y
# apt-get install -y python3-sphinx
# apt install -y libunwind-dev
# ./install_deps.sh

# if [[ -e "/usr/local/code/faasm/ceph/build" ]]; then
#   cd build
#   ninja install
# else
#   ./do_cmake.sh
#   cd build
#   ninja
#   ninja install
# fi

cd /ceph/build
ninja
ninja install