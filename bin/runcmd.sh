#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
PROJ_ROOT=${THIS_DIR}/..

# run this script from the leader node to run a command on all nodes in the cluster
function usage() {
  echo "USAGE:"
  echo "./bin/runcmd.sh COMMAND [CONTAINER]"
  echo "./bin/runcmd.sh 'ls -la' worker # run command on every node's container named *worker* "
  echo "./bin/runcmd.sh 'ls -la' # run command on every node's host"
}


if [[ -z "$1" ]]
then
  usage
  exit
fi

CMD=$1

if [[ -z "$2" ]]
then
  for host in $(docker node ls --format "{{.Hostname}}")
  do
    ssh ${host} ${CMD}
  done
else
  CONTAINER=$2
  for host in $(docker node ls --format "{{.Hostname}}")
  do
    ID=$(docker ps | grep ${CONTAINER} | awk '{print $1;}')
    ssh ${host} "docker exec ${ID} ${CMD}"
  done
fi

