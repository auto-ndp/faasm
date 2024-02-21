from invoke import task
import pprint
import requests

from faasmcli.util.call import (
    invoke_impl,
    status_call_impl,
    exec_graph_call_impl
)
from faasmcli.util.endpoints import get_invoke_host_port
from faasmcli.util.exec_graph import parse_exec_graph_json, plot_exec_graph
from faasmcli.tasks.redis import _do_redis_command
from faasmcli.util.load_balance_policy import RoundRobinLoadBalancerStrategy, WorkerHashLoadBalancerStrategy
from faasmcli.util.env import PYTHON_USER, PYTHON_FUNC, AVAILABLE_HOSTS_SET

LAST_CALL_ID_FILE = "/tmp/faasm_last_call.txt"


worker_address_cmd_str = _do_redis_command("smembers {}".format(AVAILABLE_HOSTS_SET), False, True, True)
ret_list = list(filter(None, worker_address_cmd_str.split("\n")))
print("WORKER_ADDRESSES: {}".format(ret_list))

rr_strategy = RoundRobinLoadBalancerStrategy(ret_list)
wh_strategy = WorkerHashLoadBalancerStrategy(ret_list)


def get_load_balance_strategy(policy):
    if policy == "round_robin":
        print("Using round robin strategy")
        return rr_strategy
    elif policy == "worker_hash":
        print("Using worker hash strategy")
        return wh_strategy
    else:
        print("Using round robin strategy as default")
        return rr_strategy


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
    forbid_ndp=False
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
        forbid_ndp=forbid_ndp
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
    forbid_ndp=False
):
    """
    Invoke a function
    """
    res = dispatch_impl(
        ctx,
        user,
        func,
        policy=policy,
        input=input,
        py=py,
        asynch=asynch,
        poll=poll,
        cmdline=cmdline,
        mpi_world_size=mpi_world_size,
        debug=debug,
        sgx=sgx,
        graph=graph,
        forbid_ndp=forbid_ndp
    )

    if asynch:
        print("Call ID: " + str(res))
        with open(LAST_CALL_ID_FILE, "w") as fh:
            fh.write(str(res))

def dispatch_impl(ctx,
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
                  forbid_ndp=False):
    balancer = get_load_balance_strategy(policy)
    host = balancer.get_next_host(user, func)
    
    port = 8080 # default invoke port
    # Polling always requires asynch
    if poll:
        asynch = True

    # Create URL and message
    url = "http://{}:{}/f/".format(host, port)
    
    if py:
        msg = {
            "user": PYTHON_USER,
            "function": PYTHON_FUNC,
            "async": asynch,
            "py_user": user,
            "py_func": func,
            "python": True,
        }
    else:
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
        msg["forbid_ndp"] = forbid_ndp
        
    print("Invoking function at {}".format(url))
    print("Payload:")
    pprint.pprint(msg)
    
    headers = {"Content-Type": "application/json"}
    
    if debug:
        print("POST URL    : {}".format(url))
        print("POST Headers: {}".format(headers))
        print("POST Data   : {}".format(input))

    response = requests.post(url, data=input, headers=headers)

    if response.status_code >= 400:
        print("Request failed: status = {}".format(response.status_code))
    else:
        print(response.text)
        
    if debug:
        print("Latency: {}s".format(response.elapsed.total_seconds()))
        
    return response.text
    


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
