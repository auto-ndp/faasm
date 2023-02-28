#!/bin/bash
# to be run from inside the faasm-cli container
# builds all components

set -v

cd /usr/local/code/faasm

pip install -r reqs.txt
cd faasmcli
pip install -e .
cd ../clients/cpp
pip install -e .
cd ../..

inv dev.cmake
sed -i 's/54/53/g' /root/.conan/data/libbacktrace/cci.20210118/_/_/export/conanfile.py
inv dev.cmake
inv dev.cc faasm_dev_tools