#!/bin/bash

echo demo/hello
for i in $(seq 10)
do
    curl -X POST -H "Content-Type: application/json" http://localhost:8080 --data-binary '{"user":"demo","function":"hello","input_data":""}' -w '%{time_total}' 2>&1 | tail
    sleep 0.2
done

echo wordcount
for i in $(seq 10)
do
    curl -X POST -H "Content-Type: application/json" http://localhost:8080 --data-binary '{"user":"ndp","function":"wordcount","input_data":"frankenmod.txt"}' -w '%{time_total}' 2>&1 | tail
    sleep 0.2
done

echo wordcount_manual_ndp
for i in $(seq 10)
do
    curl -X POST -H "Content-Type: application/json" http://localhost:8080 --data-binary '{"user":"ndp","function":"wordcount_manual_ndp","input_data":"frankenmod.txt"}' -w '%{time_total}' 2>&1 | tail
    sleep 0.2
done
