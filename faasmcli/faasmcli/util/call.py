from time import sleep
import pprint

from faasmcli.util.env import PYTHON_USER, PYTHON_FUNC, AVAILABLE_HOSTS_SET

from faasmcli.util.http import do_post
from faasmcli.util.endpoints import get_invoke_host_port
from faasmcli.tasks.redis import all_workers, upload_load_balancer_state, get_load_balancer_state
from faasmcli.util.load_balance_policy import RoundRobinLoadBalancerStrategy, WorkerHashLoadBalancerStrategy
STATUS_SUCCESS = "SUCCESS"
STATUS_FAILED = "FAILED"
STATUS_RUNNING = "RUNNING"

POLL_INTERVAL_MS = 1000

#worker_list = all_workers(None, local=False, docker=True, k8s=True)

worker_list = all_workers(local=False, docker=True, k8s=True)
print(worker_list)
rr_strategy = RoundRobinLoadBalancerStrategy(workers=worker_list)
wh_strategy = WorkerHashLoadBalancerStrategy(workers=worker_list)


def get_load_balance_strategy(policy):
    if policy == "round_robin":
        obj = get_load_balancer_state(policy, local=False, docker=True, k8s=True)
        if obj is None:
            return rr_strategy
        else:
            if obj.get_num_workers() != len(worker_list):
                return rr_strategy # Use local strategy
            return obj
    elif policy == "worker_hash":
        obj = get_load_balancer_state(policy, local=False, docker=True, k8s=True)
        if obj is None:
            return wh_strategy
        else:
            if obj.get_num_workers() != len(worker_list):
                return wh_strategy
            return obj
    else:
        print("Using round robin strategy as default")
        obj = get_load_balancer_state("round_robin", local=False, docker=True, k8s=True)
        if obj is None:
            return rr_strategy
        else:
            return obj

def _do_invoke(user, func, host, port, func_type, input=None):
    url = "http://{}:{}/{}/{}/{}".format(host, port, func_type, user, func)
    print("Invoking {}".format(url))
    do_post(url, input, json=True)


def _async_invoke(url, msg, headers=None, poll=False, host=None, port=None):
    # Submit initial async call
    async_result = do_post(url, msg, headers=headers, quiet=True, json=True)

    try:
        call_id = int(async_result)
    except ValueError:
        raise RuntimeError(
            "Could not parse async response to int: {}".format(async_result)
        )

    # Return the call ID if we're not polling
    if not poll:
        return call_id

    print("\n---- Polling {} ----".format(call_id))

    # Poll status until we get success/ failure
    result = None
    output = None
    count = 0
    while result != STATUS_SUCCESS:
        count += 1

        interval = float(POLL_INTERVAL_MS) / 1000
        sleep(interval)

        result, output = status_call_impl(
            msg["user"], msg["function"], call_id, host, port, quiet=True
        )
        print("\nPOLL {} - {}".format(count, result))

    print("\n---- Finished {} ----\n".format(call_id))
    print(output)

    if result == STATUS_SUCCESS:
        prefix = "SUCCESS:"
    else:
        prefix = "FAILED:"

    output = output.replace(prefix, "")

    return call_id


def invoke_impl(
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
    forbid_ndp=False,
):
    host, port = get_invoke_host_port()

    # Polling always requires asynch
    if poll:
        asynch = True

    # Create URL and message
    url = "http://{}".format(host)
    if not port == "80":
        url += ":{}".format(port)

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

    if asynch:
        return _async_invoke(url, msg, poll=poll, host=host, port=port)
    else:
        return do_post(url, msg, json=True, debug=debug)

def dispatch_impl(user,
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
    
    upload_load_balancer_state(balancer, policy, docker=True) # Allows the load balancer to keep state between calls
    
    port = 8080 # default invoke port
    # Polling always requires asynch
    if poll:
        asynch = True

    # Create URL and message
    url = "http://{}".format(host)
    if not port == "80":
        url += ":{}".format(port)

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

    if asynch:
        return _async_invoke(url, msg, poll=poll, host=host, port=port)
    else:
        return do_post(url, msg, json=True, debug=debug)


def status_call_impl(user, func, call_id, host, port, quiet=False):
    msg = {
        "user": user,
        "function": func,
        "status": True,
        "id": int(call_id),
    }
    call_result = _do_single_call(host, port, msg, quiet)

    if call_result.startswith("SUCCESS"):
        return STATUS_SUCCESS, call_result
    elif call_result.startswith("FAILED"):
        return STATUS_FAILED, call_result
    else:
        return STATUS_RUNNING, call_result


def exec_graph_call_impl(call_id, host, port, quiet=False):
    msg = {
        "user": "",
        "function": "",
        "exec_graph": True,
        "id": int(call_id),
    }
    call_result = _do_single_call(host, port, msg, quiet)

    if not quiet:
        print(call_result)

    return call_result


def _do_single_call(host, port, msg, quiet):
    url = "http://{}".format(host)
    if port != 80:
        url += ":{}/".format(port)

    return do_post(url, msg, quiet=quiet, json=True)
