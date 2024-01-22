import requests
import matplotlib.pyplot as plt
import time
import sys

def send_request(worker_url):
    try:
        response = requests.get(worker_url)
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
    worker_url = sys.argv[1]
    request_frequency = sys.argv[2]
    duration = sys.argv[3]
    run_wordcount_benchmark(worker_url, request_frequency, duration)
    
