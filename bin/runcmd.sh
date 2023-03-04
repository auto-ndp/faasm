#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
PROJ_ROOT=${THIS_DIR}/..

# run this script from the leader node to run a command on all nodes in the cluster
CMD=$1

for host in $(docker node ls --format "{{.Hostname}}")
do
  ssh ${host} ${CMD}
done