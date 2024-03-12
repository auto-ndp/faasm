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

# Start sar in the background to monitor CPU utilization
sar -u 1 > cpu_utilization.log &

# Get the process ID of sar
sar_pid=$!

# Run fio
fio --profile=tiobench > /dev/null 2>&1 &

# Ask the user for the time interval to run sar
time_interval=$1

# Sleep for the specified time interval
sleep $time_interval

# Kill sar after the specified time interval
pkill -P $sar_pid sar

echo "sar process has been terminated."

exit 0
