#!/bin/bash

# Get the container ID or name
container_id=$(docker ps | grep faasm_cpp | awk '{print $1;}')
benchmark_script_name="$1"
benchmark_script_host="./$benchmark_script_name"
destination_directory="/code/cpp/bin"

# Check if the container is running
if [ -n "$container_id" ]; then
    # Copy the benchmark script into the container
    docker cp "$benchmark_script_host" "$container_id:$destination_directory/"
    
    echo "$benchmark_script_name"
    # Enter the running Docker container
    docker exec -i "$container_id" /bin/bash -c "cd $destination_directory && pwd && chmod +x $benchmark_script_name && source ./$benchmark_script_name"
    #docker exec -it "$container_id" /bin/bash -c "ls -l /code/cpp"

else
    echo "Error: Container faasm_cpp is not running."
fi