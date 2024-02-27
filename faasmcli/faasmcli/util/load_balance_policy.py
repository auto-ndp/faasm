from abc import ABC, abstractmethod
import itertools

class ILoadBalanceStrategy(ABC):
    
    @abstractmethod
    def get_next_host(self, user=None, func_name=None):
        pass
    
    @abstractmethod
    def get_num_workers(self):
        pass
    

class RoundRobinLoadBalancerStrategy(ILoadBalanceStrategy):
    def __init__(self, workers):
        self.workers = workers
        self.worker_iterator = itertools.cycle(self.workers)
        print("Creating RoundRobinLoadBalancerStrategy with {} workers".format(len(workers)))
        
    def get_next_host(self, user=None, func_name=None):
        return next(self.worker_iterator)
    
    def get_num_workers(self):
        return len(self.workers)
    
class WorkerHashLoadBalancerStrategy(ILoadBalanceStrategy):
    def __init__(self, workers):
        self.workers = workers

    def get_next_host(self, user=None, func=None) -> str:
        # Calculate the hash of the task ID
        #hash_value = hash(user + func)

        # Get the index of the worker based on the hash value
        #worker_index = hash_value % len(self.workers)

        #Return the worker ID
        return self.workers[0]

    def get_num_workers(self):
        return len(self.workers)
