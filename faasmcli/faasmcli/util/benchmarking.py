from invoke import task
from faasmcli.util.call import (
    get_load_balance_strategy,
    upload_load_balancer_state,
)

import requests

# Create task for invoking a function asynchronously
import time
import aiohttp
import asyncio
import threading

from queue import Queue

from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import deque
import itertools

def batch_async_aiohttp(msg, headers, selected_balancer, n, forbid_ndp):
    ITERATIONS = int(n)
    results = []
    for i in range(ITERATIONS-1, ITERATIONS):
        latencies = []
        start_time = time.perf_counter()
        print("Running a batch of size: ", i)
        print("Selected balancer: ", selected_balancer)
        latencies = asyncio.run(batch_send(msg, headers, i, selected_balancer))
        end_time = time.perf_counter()
        print("Time taken to run batch: ", end_time - start_time)
        result_dict = {
            "batch_size": i,
            "mean_latency" : sum(latencies)/len(latencies),
            "median_latency": sorted(latencies)[len(latencies)//2],
            "time_taken": end_time - start_time            
        }
        
        print("Result: ", result_dict)
        results.append(result_dict)
    
    # timestamp as str
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    
    result_name = "{}_{}_{}_ndp_{}_iters_{}.csv".format(timestamp , msg["function"], selected_balancer, not forbid_ndp, n)
    write_to_file("./experiments/" + result_name, results)    
    print("Done running batches")
    return results

async def batch_send(data, headers, n, selected_balancer):
    async with aiohttp.ClientSession() as session:
        tasks = []
        for _ in range(n):
            with threading.Lock():
                balancer = get_load_balance_strategy(selected_balancer)
                worker_id = balancer.get_next_host(data["user"], data["function"])
                url = "http://{}:{}/f/".format(worker_id, 8080)
                tasks.append(dispatch_func_async(session, url, data, headers))
                upload_load_balancer_state(balancer, selected_balancer, docker=True) # Allows the load balancer to keep state between calls
            #await asyncio.sleep(1/n)
        responses= await asyncio.gather(*tasks)
    return responses

async def dispatch_func_async(session, url, data, headers):
    start_time = time.perf_counter()
    async with session.post(url, json=data, headers=headers) as response:
        end_time = time.perf_counter()
        await response.text()
        return end_time - start_time        
        
# =============================================================================
# Sliding Window distribution
# =============================================================================

def post_request(url, data, headers):
    response = requests.post(url, json=data, headers=headers)
    latency = response.elapsed.total_seconds()
    return response.text, latency

def sliding_window_impl(tasks, n, max_parallel):
    latencies = []
    lock = threading.Lock()

    # Worker function to process tasks
    def worker():
        while not tasks.empty():
            task = tasks.get()
            text, latency = post_request(*task)
            with lock:
                latencies.append(latency)
                remaining_tasks = tasks.qsize()
                print("Lantecy: ", latency)
                print(f"Tasks left: {tasks.qsize()}")
            tasks.task_done()

    batch_time_start = time.perf_counter()
    # Start max_parallel threads
    print("Starting {} threads".format(max_parallel))
    threads = []
    for _ in range(max_parallel):
        t = threading.Thread(target=worker)
        t.start()
        threads.append(t)

    # Wait for all tasks to be processed
    print("Waiting for tasks to finish")
    for t in threads:
        t.join()

    # Wait for all threads to finish
    tasks.join()
    batch_time_end = time.perf_counter()
    
    print("Total time taken: ", batch_time_end - batch_time_start)
    print("Throughput (requests/second): ", n/(batch_time_end - batch_time_start))
    print("Median Latency: ", sorted(latencies)[len(latencies)//2])
    print("Mean latency: ", sum(latencies)/len(latencies))
    print("Responses received: ", len(latencies))