#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
PROJ_ROOT=${THIS_DIR}/..
NODE=WORKER

OSDSIZE="${OSDSIZE:-500G}"

# execute from each node before initial deployment
function setup {
  dd if=/dev/zero of=/ceph.img bs=1 count=0 seek=${OSDSIZE}
  mkfs.ext4 /ceph.img
  losetup /dev/loop30 /ceph.img
  mkdir -p /mnt/ceph
  mount -o user_xattr /dev/loop30 /mnt/ceph

  rm ${PROJ_ROOT}/dev/faasm-local/ceph-ceph-mon1/*
}

# execute from leader node AFTER deploying docker stack
function syncleader {
  LEADERHOST=$(docker node ls --format "{{.Hostname}}" --filter node.label=rank=leader)
  for host in $(docker node ls --format "{{.Hostname}}")
  do
    if [[ ${host} != ${LEADERHOST} ]]
    then
      scp ${PROJ_ROOT}/dev/faasm-local/ceph-ceph-mon1/* ${host}:${PROJ_ROOT}/dev/faasm-local/ceph-ceph-mon1/
    fi
  done
}

function clean {
  # rm /mnt/ceph1 /mnt/ceph2 /mnt/ceph3 /mnt/ceph4
  umount /mnt/ceph
  rm -r /mnt/ceph
  losetup -d /dev/loop30
  rm /ceph.img
}

docker node ls 1>/dev/null 2>/dev/null
if [ $(echo $?) == 0 ]
then
  NODE=LEADER
fi

if [[ $1 == sync && ${NODE} == LEADER ]]
then 
  syncleader
fi

if [ $1 == clean ]
then 
  clean
fi

if [ $1 == setup ]
then 
  setup
fi