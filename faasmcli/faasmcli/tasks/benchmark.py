from invoke import task

from faasmcli.util.benchmarking import batch_async_aiohttp, sliding_window_impl, run_benchmark_multiple_objs
from faasmcli.util.call import get_load_balance_strategy

from queue import Queue

@task
def throughput_test(
    ctx,
    user,
    func,
    iters=10,
    forbid_ndp=False,
    policy="round_robin",
    input=None,
    py=False,
    asynch=False,
    poll=False,
    cmdline=None,
    mpi_world_size=None,
    debug=False,
    sgx=False,
    graph=False,
):

    if poll:
        asynch = True
        
    msg = {
        "user": user,
        "function": func,
        "async": asynch,
    }

    if sgx:
        msg["sgx"] = sgx

    if input:
        msg["input_data"] = input

    if cmdline:
        msg["cmdline"] = cmdline

    if mpi_world_size:
        msg["mpi_world_size"] = int(mpi_world_size)

    if graph:
        msg["record_exec_graph"] = graph
        
    if forbid_ndp:
        print("Forbid NDP: ", forbid_ndp)
        msg["forbid_ndp"] = forbid_ndp
    print("Payload:", msg)
    return batch_async_aiohttp(msg, {"Content-Type": "application/json"}, policy, iters, forbid_ndp)

@task
def latency_test(
        ctx,
    user,
    func,
    iters=10,
    parallel=20,
    forbid_ndp=False,
    policy="round_robin",
    input=None,
    py=False,
    asynch=False,
    poll=False,
    cmdline=None,
    mpi_world_size=None,
    debug=False,
    sgx=False,
    graph=False,
):

    if poll:
        asynch = True
        
    msg = {
        "user": user,
        "function": func,
        "async": asynch,
    }

    if sgx:
        msg["sgx"] = sgx

    if input:
        msg["input_data"] = input

    if cmdline:
        msg["cmdline"] = cmdline

    if mpi_world_size:
        msg["mpi_world_size"] = int(mpi_world_size)

    if graph:
        msg["record_exec_graph"] = graph
        
    if forbid_ndp:
        print("Forbid NDP: ", forbid_ndp)
        msg["forbid_ndp"] = forbid_ndp
    print("Payload:", msg)
    
    tasks = Queue()
    headers = {"Content-Type": "application/json"}
    
    # Populate the queue with tasks
    balancer = get_load_balance_strategy(policy)
    print("Populating queue with {} tasks".format(iters))
    for _ in range(iters):
        worker_id = balancer.get_next_host(forbid_ndp)
        url = format_worker_url(worker_id)
        tasks.put((url, msg, headers))
    return sliding_window_impl(tasks, iters, parallel)


@task
def throughput_test_multiple_objects(
    ctx,
    user,
    func,
    iters=10,
    max_parallel=20,
    forbid_ndp=False,
    policy="round_robin",
    inputs=None,
    py=False,
    asynch=False,
    poll=False,
    cmdline=None,
    mpi_world_size=None,
    debug=False,
    sgx=False,
    graph=False,
):
    
    if poll:
        asynch = True
        
    msg = {
        "user": user,
        "function": func,
        "async": asynch,
    }

    if sgx:
        msg["sgx"] = sgx

    if input:
        msg["input_data"] = "CHANGE_ME"

    if cmdline:
        msg["cmdline"] = cmdline

    if mpi_world_size:
        msg["mpi_world_size"] = int(mpi_world_size)

    if graph:
        msg["record_exec_graph"] = graph
        
    if forbid_ndp:
        print("Forbid NDP: ", forbid_ndp)
        msg["forbid_ndp"] = forbid_ndp
    print("Payload:", msg)
    
    inputs_splitted = inputs.split(",")
    
    tasks = Queue()
    headers = {"Content-Type": "application/json"}
    balancer = get_load_balance_strategy(policy)
    print("Populating queue with {} tasks".format(iters))
    for i in range(iters):
        worker = balancer.get_next_host(forbid_ndp)
        url = format_worker_url(worker)
        msg["input_data"] = inputs_splitted[i % len(inputs_splitted)]
        print("Input data: ", msg["input_data"])
        tasks.put((url, msg, headers))
    return sliding_window_impl(tasks, iters, max_parallel)

def format_worker_url(worker_id):
    return "http://{}:{}/f/".format(worker_id, 8080)
