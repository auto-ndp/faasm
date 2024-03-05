from abc import ABC, abstractmethod
import itertools
from faasmcli.util.endpoints import get_invoke_host_port

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
    
class FaasmDefault(ILoadBalanceStrategy):
    def __init__(self, workers):
        self.workers = workers

    def get_next_host(self, user=None, func=None) -> str:
        return get_invoke_host_port()[0] # Only return the worker host

    def get_num_workers(self):
        return len(self.workers)
