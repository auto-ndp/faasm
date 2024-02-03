#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
PROJ_ROOT=${THIS_DIR}/..
NODE=WORKER

OSDSIZE="${OSDSIZE:-80G}"

# execute from each node before initial deployment
function setup {
  dd if=/dev/zero of=/ceph.img bs=1 count=0 seek=${OSDSIZE}
  mkfs.ext4 /ceph.img
  losetup /dev/loop30 /ceph.img
  mkdir -p /mnt/ceph
  mount -o user_xattr /dev/loop30 /mnt/ceph

  rm -f ${PROJ_ROOT}/dev/faasm-local/ceph-ceph-mon1/*
  rm -f ${PROJ_ROOT}/dev/container/shared_store/osd*
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
  set -euo pipefail

  # Stop Ceph services (adjust commands if you're using systemd or another init system)
  pkill ceph-mon || true
  pkill ceph-mgr || true
  pkill ceph-osd || true

  echo "Ceph services have been stopped."

  # Wait a bit to ensure the processes have stopped
  sleep 5

  # Remove Ceph data directories
  NODE_CEPH_DIR="/usr/local/faasm/ceph-$(hostname)"
  rm -rf "${NODE_CEPH_DIR}"
  echo "Ceph data directories have been removed."

  # Remove symlinks created during setup
  rm -f /etc/ceph/ceph.client.admin.keyring
  rm -f /var/lib/ceph/bootstrap-osd/ceph.keyring
  echo "Ceph symlinks have been removed."

  # Clean up Ceph monitor and manager directories
  sudo rm -rf /var/lib/ceph/mon/ceph-$(hostname -s)
  sudo rm -rf /var/lib/ceph/mgr/ceph-$(hostname -s)
  echo "Ceph monitor and manager directories have been removed."

  # Unmount and remove OSD storage
  umount /mnt/ceph || true
  rm -r /mnt/ceph || true
  losetup -d /dev/loop30 || true
  rm /ceph.img || true
  echo "OSD storage has been unmounted and removed."

  # Clean up any remaining Ceph files
  sudo rm -rf /var/lib/ceph/
  sudo rm -rf /etc/ceph/
  echo "Ceph has been wiped from the system."
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