from abc import ABC, abstractmethod
import itertools

class ILoadBalanceStrategy(ABC):
    
    @abstractmethod
    def get_next_host(self, user=None, func_name=None):
        pass
    

class RoundRobinLoadBalancerStrategy(ILoadBalanceStrategy):
    
    def __init__(self, workers):
        print("Workers are: ", workers)
        self.workers = workers
        self.worker_iterator = itertools.cycle(workers)
        print("Creating RoundRobinLoadBalancerStrategy with {} workers".format(len(workers)))
        
    def get_next_host(self, user=None, func_name=None):
        print("Workers are: ", self.workers)
        print("Current worker iterator: ", self.worker_iterator)
        host = next(self.worker_iterator)
        print("Returning host: ", host)
        return host
    
class WorkerHashLoadBalancerStrategy(ILoadBalanceStrategy):
    def __init__(self, workers):
        self.workers = workers

    def get_next_host(self, user=None, func=None) -> str:
        # Calculate the hash of the task ID
        hash_value = hash(user + func)

        # Get the index of the worker based on the hash value
        worker_index = hash_value % len(self.workers)

        #Return the worker ID
        return self.workers[worker_index]
