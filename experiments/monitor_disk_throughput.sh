#!/bin/bash

# Define the size of the block (in bytes)
block_size=1048576  # 1 MB

# Generate a block of variable size filled with random data
generate_random_block() {
    dd if=/dev/urandom bs="$block_size" count=1 status=none
}

# Read block traces from the dataset
read_traces() {
    cat "$1"
}

# Parse block traces and perform operations on the block
parse_and_apply_traces() {
    local block="$1"
    while IFS=, read -r operation offset size; do
        if [[ "$operation" == "read" ]]; then
            echo -n "Performing read operation at offset $offset with size $size... "
            echo "$block" | dd bs=1 count="$size" skip="$offset" 2>/dev/null
        elif [[ "$operation" == "write" ]]; then
            echo -n "Performing write operation at offset $offset with size $size... "
            echo "$block" | dd of=/dev/null bs=1 count="$size" seek="$offset" 2>/dev/null
        fi
    done < "$2"
}

# Main function
main() {
    # Create a block of random data
    echo "Generating block of size $block_size..."
    block=$(generate_random_block)
    
    # Apply block traces to the block
    traces_file="block_traces.csv"  # Path to block traces dataset
    echo "Applying block traces from $traces_file..."
    parse_and_apply_traces "$block" "$traces_file"
}

# Run the main function
main
