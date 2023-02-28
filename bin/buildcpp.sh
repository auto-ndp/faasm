#!/bin/bash
# to be run from inside the cpp container
# builds all components

set -v

pip install -r requirements.txt
cp WasiToolchain.cmake /usr/local/faasm/toolchain/tools/WasiToolchain.cmake