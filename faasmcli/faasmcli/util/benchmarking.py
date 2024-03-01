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

from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import deque

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

def write_to_file(fp, results):
    with open(fp, "a") as f:
        
        f.write("Batch size,Mean Latency,Median Latency,Time taken\n")
        for dict in results:        
            f.write(str(dict["batch_size"]) + ",")
            f.write(str(dict["mean_latency"]) + ",")
            f.write(str(dict["median_latency"]) + ",")
            f.write(str(dict["time_taken"]) + "\n")
        f.write("\n")
        
        
# =============================================================================
# Sliding Window distribution
# =============================================================================

def post_request(url, data, headers):
    with requests.post(url, json=data, headers=headers) as response:
        return response

def format_worker_url(worker_id):
    return "http://{}:{}/f/".format(worker_id, 8080)

def sliding_window_impl(msg, headers, selected_balancer, n, forbid_ndp):
    results = []
    num_parallel = 20
    
    with ThreadPoolExecutor(max_workers=num_parallel) as executor:
        url_queue = deque()
        balancer = get_load_balance_strategy(selected_balancer)
        # Fill the initial sliding window
        for _ in range(num_parallel):
            worker_id = balancer.get_next_host(msg["user"], msg["function"])
            url = format_worker_url(worker_id)
            future = executor.submit(post_request, url, msg, headers)
            url_queue.append((url, future))
        
        while len(results) < n:
            # Wait for any of the futures to complete
            done, pending = as_completed([future for _, future in url_queue]).__next__()
            
            # Find the completed future and its corresponding URL
            for index, (url, future) in enumerate(url_queue):
                if future == done:
                    # Store the result
                    result = future.result()
                    results.append(result)
                    
                    if len(results) < n:
                        # Replace completed future with a new one
                        worker_id = selected_balancer.get_next_host(msg["user"], msg["function"])
                        new_url = format_worker_url(worker_id)
                        new_future = executor.submit(post_request, new_url, msg, headers)
                        url_queue[index] = (new_url, new_future)
                    
                    break  # Break out of loop once the completed future is found
    
    return results