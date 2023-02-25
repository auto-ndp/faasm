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

bash