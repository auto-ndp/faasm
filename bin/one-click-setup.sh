#!/bin/bash

sudo -s /bin/bash
PROJ_ROOT=~/faasm
cd ${PROJ_ROOT} # assumed PROJ_ROOT (env vars may be lost since we became sudo)

apt-get -y update
apt-get -y install ca-certificates curl gnupg lsb-release

mkdir -m 0755 -p /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg

echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

apt-get -y update
apt-get -y install python3-venv
apt-get -y install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# not needed since 'sudo'
# sudo groupadd docker
# sudo usermod -aG docker $USER
# newgrp docker

source ./bin/cluster_env.sh
source ./bin/workon.sh
./bin/refresh_local.sh
./bin/cli.sh build-faasm

./deploy/local/dev_cluster.sh

docker compose down