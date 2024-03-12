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

# Run fio without displaying output
fio --profile=tiobench --numjobs=12 --blocksize=8k --iodepth=64 --size=10G > /dev/null 2>&1 &

# Get the process ID of fio
fio_pid=$!

# Ask the user for the time interval to run iostat
time_interval=$1

# Start the countdown
echo "Experiment will end in $time_interval seconds."

# Sleep for the specified time interval
for ((i=$time_interval; i>0; i--)); do
    echo -ne "\rTime remaining: $i seconds"
    sleep 1
done
echo -ne "\n"

# Kill iostat after the specified time interval
pkill -P $iostat_pid iostat

echo "iostat process has been terminated."

# Kill fio after the specified time interval
kill $fio_pid

echo "fio process has been terminated."

# Calculate and print the average disk throughput
avg_disk_throughput=$(awk '/^sda/ {sum += $6} END {print "Average Disk Throughput:", sum/NR}' disk_throughput.log)
echo $avg_disk_throughput

exit 0
