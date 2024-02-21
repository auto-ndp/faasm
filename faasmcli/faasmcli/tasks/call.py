from invoke import task

from faasmcli.util.call import (
    invoke_impl,
    status_call_impl,
    exec_graph_call_impl,
    dispatch_impl
)
from faasmcli.util.endpoints import get_invoke_host_port
from faasmcli.util.exec_graph import parse_exec_graph_json, plot_exec_graph
from faasmcli.util.load_balance_policy import RoundRobinLoadBalancerStrategy, WorkerHashLoadBalancerStrategy
from faasmcli.tasks.redis import all_workers

LAST_CALL_ID_FILE = "/tmp/faasm_last_call.txt"

# WORKER_ADDRESSES = get_worker_addresses() # Parse the config for list of all local workers in cluster
# round_robin_strategy = RoundRobinLoadBalancerStrategy(WORKER_ADDRESSES) # Create a round robin load balancer strategy
# worker_hash_strategy = WorkerHashLoadBalancerStrategy(WORKER_ADDRESSES) # Create a worker hash load balancer strategy



@task(default=True, name="invoke")
def invoke(
    ctx,
    user,
    func,
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
    """
    Invoke a function
    """
    res = invoke_impl(
        user,
        func,
        input=input,
        py=py,
        asynch=asynch,
        poll=poll,
        cmdline=cmdline,
        mpi_world_size=mpi_world_size,
        debug=debug,
        sgx=sgx,
        graph=graph,
    )

    if asynch:
        print("Call ID: " + str(res))
        with open(LAST_CALL_ID_FILE, "w") as fh:
            fh.write(str(res))

@task
def dispatch(
    ctx,
    user,
    func,
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
    """
    Invoke a function
    """
    
    # Get worker based on policy
    # if policy == "round_robin":
    #     host = round_robin_strategy.get_next_host(user, func)
    # elif policy == "worker_hash":
    #     host = worker_hash_strategy.get_next_host(user, func)
    # else:
    #     host = round_robin_strategy.get_next_host(user, func)
    
    host="worker-0"
    print("Dispatching to worker: {}".format(host))
    port = 8080 # default invoke port
    res = dispatch_impl(
        user,
        func,
        host,
        port,
        input=input,
        py=py,
        asynch=asynch,
        poll=poll,
        cmdline=cmdline,
        mpi_world_size=mpi_world_size,
        debug=debug,
        sgx=sgx,
        graph=graph,
    )

    if asynch:
        print("Call ID: " + str(res))
        with open(LAST_CALL_ID_FILE, "w") as fh:
            fh.write(str(res))

def get_call_id(call_id):
    if not call_id:
        with open(LAST_CALL_ID_FILE) as fh:
            call_id = fh.read()

        if not call_id:
            print("No call ID provided and no last call ID found")
            exit(1)

    return call_id


@task
def status(ctx, call_id=None):
    """
    Get the status of an async function call
    """
    host, port = get_invoke_host_port()
    call_id = get_call_id(call_id)
    status_call_impl(None, None, call_id, host, port, quiet=False)


@task
def exec_graph(ctx, call_id=None, headless=False, output_file=None):
    """
    Get the execution graph for the given call ID
    """
    host, port = get_invoke_host_port()
    call_id = get_call_id(call_id)

    json_str = exec_graph_call_impl(call_id, host, port, quiet=True)

    graph = parse_exec_graph_json(json_str)

    if output_file:
        plot_exec_graph(graph, headless=headless, output_file=output_file)
    else:
        plot_exec_graph(graph, headless=headless)
