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
sar -u 2 > cpu_utilization.log &

# Get the process ID of sar
sar_pid=$!

# Run fio without displaying output
fio --profile=tiobench --numjobs=12 --blocksize=8k --iodepth=32 > /dev/null 2>&1 &

# Get the process ID of fio
fio_pid=$!

# Ask the user for the time interval to run sar
time_interval=$1

# Start the countdown
echo "Experiment will end in $time_interval seconds."

# Sleep for the specified time interval
for ((i=$time_interval; i>0; i--)); do
    echo -ne "\rTime remaining: $i seconds"
    sleep 1
done
echo -ne "\n"

# Kill sar after the specified time interval
pkill -P $sar_pid sar

echo "sar process has been terminated."

# Kill fio after the specified time interval
kill $fio_pid

echo "fio process has been terminated."

# Calculate and print the average CPU utilization
avg_cpu_util=$(awk '{sum += $NF} END {print "Average CPU Utilization:", sum/NR}' cpu_utilization.log)
echo $avg_cpu_util

exit 0
