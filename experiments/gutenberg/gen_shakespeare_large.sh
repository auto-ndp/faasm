#!/bin/bash

# Define the file paths
source_file="${FAASM_ROOT}/experiments/gutenberg/complete_works_shakespeare.txt"
destination_file="${FAASM_ROOT}/experiments/gutenberg/shakespeare_large.txt"

# Define the number of times to append
n=22

if [ ! -e "$destination_file" ]; then
    touch "$destination_file"
fi


# Loop n times and append the contents of source file to destination file
for ((i=0; i<$n; i++))
do
    cat "$source_file" >> "$destination_file"
done

echo "Contents of $source_file appended to $destination_file $n times."
