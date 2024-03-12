#!/bin/bash

# Function to display usage information
usage() {
    echo "Usage: $0 <time_interval>"
    echo "Example: $0 60   # Kills iostat after 60 seconds"
    exit 1
}

# Check if the user provided the time interval argument
if [ $# -ne 1 ]; then
    usage
fi

# Start iostat in the background to monitor disk throughput
iostat -dkx 1 > disk_throughput.log &

# Get the process ID of iostat
iostat_pid=$!

# Run fio
fio --profile=tiobench

# Ask the user for the time interval to run iostat
time_interval=$1

# Sleep for the specified time interval
sleep $time_interval

# Kill iostat after the specified time interval
pkill -P $iostat_pid iostat

echo "iostat process has been terminated."

exit 0
