from invoke import task

from faasmcli.util.benchmarking import batch_async_aiohttp, sliding_window_impl, run_benchmark_multiple_objs


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
    return sliding_window_impl(msg, {"Content-Type": "application/json"}, policy, iters, parallel, forbid_ndp)


@task
def throughput_test_multiple_objects(
    ctx,
    user,
    func,
    iters=10,
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
        msg["input_data"] = input.split(",")

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
    return run_benchmark_multiple_objs(msg, {"Content-Type": "application/json"}, iters, policy, forbid_ndp)
