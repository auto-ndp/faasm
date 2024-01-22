#!bin/bash

# Configuration
url='http://worker-0:8080/f/'
content_type='Content-Type: application/json'
data='{"async": false, "user": "ndp", "function": "wordcount", "input_data": "key"}'
requests_count=100  # Number of requests to send

# Create an array to store response times
declare -a response_times

# Loop to send requests
for ((i=1; i<=$requests_count; i++)); do
    start_time=$(date +%s%N)  # Start time in nanoseconds
    response=$(curl -s -X POST "$url" -H "$content_type" -d "$data")
    end_time=$(date +%s%N)    # End time in nanoseconds

    if [ $? -eq 0 ]; then
        # Calculate response time in seconds
        response_time=$(echo "scale=6; ($end_time - $start_time)" | awk '{printf "%.10f", $1}')
        response_times+=("$response_time")
        echo "Request $i: $response_time seconds"
    else
        echo "Request $i failed."
        echo "Response content: $response"
    fi
done

