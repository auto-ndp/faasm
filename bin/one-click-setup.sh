#!/bin/bash

THIS_DIR=$(dirname $(readlink -f ${BASH_SOURCE[0]:-${(%):-%x}}))
PROJ_ROOT=${THIS_DIR}/..
cd ${PROJ_ROOT} 

apt-get -y update
apt-get -y install ca-certificates curl gnupg lsb-release

mkdir -m 0755 -p /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

apt-get -y update
apt-get -y install python3-venv
apt-get -y install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# not needed since running as 'root'
# sudo groupadd docker
# sudo usermod -aG docker $USER
# newgrp docker

source ./bin/cluster_env.sh
source ./bin/workon.sh
./bin/refresh_local.sh

# OSD setup
for id in 0 1 2 3 4
do
  dd if=/dev/zero of=/ceph${id}.img bs=1 count=0 seek=100G
  mkfs.ext4 /ceph${id}.img
  losetup /dev/loop3${id} /ceph${id}.img
  mkdir -p /mnt/ceph${id}
  mount -o user_xattr /dev/loop3${id} /mnt/ceph${id}
done