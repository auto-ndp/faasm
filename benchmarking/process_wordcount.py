import requests
import matplotlib.pyplot as plt
import time
import sys

def send_request(worker_url):
    headers = {'Content-Type': 'application/json'}
    data = {
        'async': False,
        'user': 'ndp',
        'function': 'wordcount',
        'input_data': 'key'
    }

    try:
        response = response = requests.post(worker_url, headers=headers, json=data)
        if response.status_code == 200:
            return response.elapsed.total_seconds()  # Latency in seconds
        else:
            return None
    except requests.RequestException as e:
        print(f"Error sending request: {e}")
        return None

def run_wordcount_benchmark(worker_url, request_frequency, duration):
    latencies = []
    start_time = time.time()
    while time.time() - start_time < int(duration):
        latency = send_request(worker_url)
        if latency:
            latencies.append(latency)
        time.sleep(1 / int(request_frequency))
    
    plt.plot(latencies)
    plt.ylabel("Latency (s)")
    plt.xlabel("Request Frequency (Hz)")
    plt.title("Wordcount Benchmark")
    plt.show()    
    

if __name__ == "__main__":
    worker_url = "http://worker-0:8080/f/"
    request_frequency = 10
    duration = 30
    run_wordcount_benchmark(worker_url, request_frequency, duration)
    
