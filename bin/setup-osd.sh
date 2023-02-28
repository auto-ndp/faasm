#!/bin/bash
set -e

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
PROJ_ROOT=${THIS_DIR}/..
NODE=WORKER

# execute from each node before initial deployment
function setup {
  for id in 0 1 2 3 4
  do
    dd if=/dev/zero of=/ceph${id}.img bs=1 count=0 seek=100G
    mkfs.ext4 /ceph${id}.img
    losetup /dev/loop3${id} /ceph${id}.img
    mkdir -p /mnt/ceph${id}
    mount -o user_xattr /dev/loop3${id} /mnt/ceph${id}
  done
  rm dev/faasm-local/ceph-ceph-mon1/*
}

# setup worker
# function setupworker {
#   for id in 0 1 2 3 4
#   do
#     losetup /dev/loop3${id} /ceph${id}.img
#     mkdir -p /mnt/ceph${id}
#     mount -o user_xattr /dev/loop3${id} /mnt/ceph${id}
#   done
# }

# execute from leader node AFTER initial deployment on each node 
function sync {
  LEADERHOST=$(docker node ls --format "{{.Hostname}}" --filter node.label=rank=leader)
  for host in $(docker node ls --format "{{.Hostname}}")
  do
    if [[ ${host} != ${LEADERHOST} ]]
    then
      scp ${PROJ_ROOT}/dev/faasm-local/ceph-ceph-mon1/* ${host}:${PROJ_ROOT}/dev/faasm-local/ceph-ceph-mon1/
      for id in 0 1 2 3 4
      do
        # scp /ceph${id}.img ${host}:/ceph${id}.img
        scp -r /mnt/ceph${id} ${host}:/mnt/ceph${id}
      done
    fi
  done
}

function clean {
  for id in 0 1 2 3 4
  do
    umount /mnt/ceph${id}
    rm -r /mnt/ceph${id}
    losetup -d /dev/loop3${id}
    rm /ceph${id}.img
  done
}

docker node ls 1>/dev/null 2>/dev/null
if [ $(echo $?) == 0 ]
then
  NODE=LEADER
fi

# if [[ $1 == setup && ${NODE} == LEADER ]]
# then 
#   setupleader
# fi

if [[ $1 == sync && ${NODE} == LEADER ]]
then 
  sync
fi

if [ $1 == clean ]
then 
  clean
fi

if [ $1 == setup ]
then 
  setup
fi