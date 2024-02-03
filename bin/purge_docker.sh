#!/bin/bash

# Stop and remove all running containers
docker stop $(docker ps -aq)
docker rm $(docker ps -aq)

# Remove all images
docker rmi $(docker images -aq)

# Remove all volumes
docker volume rm $(docker volume ls -q)

# Remove all networks
docker network rm $(docker network ls -q)

# Remove all dangling images, containers, volumes, and networks
docker system prune -af

# Leave Docker swarm
docker swarm leave --force

for key in $(apt-key list | grep -Po '(?<=pub\s\s\s\s)[^/]*' | cut -d' ' -f2); do
  sudo apt-key del $key
done