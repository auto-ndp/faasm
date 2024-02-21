from subprocess import call
import pickle

from invoke import task
from os import environ
from faasmcli.util.env import PROJ_ROOT, AVAILABLE_HOSTS_SET
from subprocess import check_output, CalledProcessError

def _do_redis_command(sub_cmd, host, local, docker, k8s):
    redis_host = environ.get(host, "redis")
    if local:
        cmd = ["redis-cli", sub_cmd]
    elif docker:
        cmd = ["redis-cli", "-h", redis_host, sub_cmd]
    elif k8s:
        cmd = [
            "kubectl",
            "exec",
            "-n faasm",
            "redis-queue",
            "--",
            "redis-cli",
            sub_cmd,
        ]
    else:
        cmd = ["redis-cli", sub_cmd]

    cmd_string = " ".join(cmd)
    print(cmd_string)
    try:
        output = check_output(cmd_string, shell=True, cwd=PROJ_ROOT)
        return output.decode('utf-8')
    except CalledProcessError as e:
        print(f"Command failed: {cmd_string}")
        return None

        
@task
def clear_queue(ctx, local=False, docker=False, k8s=True):
    """
    Clear the message queue in Redis
    """
    _do_redis_command("flushall", "REDIS_QUEUE_HOST", local, docker, k8s)


@task
def all_workers(ctx, local=False, docker=False, k8s=True):
    """
    List all available Faasm instances
    """
    ret_str = _do_redis_command(
        "smembers {}".format(AVAILABLE_HOSTS_SET), "REDIS_QUEUE_HOST", local, docker, k8s
    )
    # format the output into a list
    ret_list = ret_str.split("\n")
    
    # Remove empty strings
    ret_list = list(filter(None, ret_list))
    return ret_list

def upload_load_balancer_state(load_balance_obj, policy, local=False, docker=False, k8s=True):
    """
    Upload the load balancer state to Redis
    """
    
    # Serialize the object to a string
    load_balance_obj_str = pickle.dumps(load_balance_obj)
    result_obj_str = _do_redis_command(
        "set {} {}".format(policy, load_balance_obj_str), "REDIS_STATE_HOST", local, docker, k8s
    )

def get_load_balancer_state(policy, local=False, docker=False, k8s=True):
    result_obj_str = _do_redis_command(
        "get {}".format(policy), "REDIS_STATE_HOST", local, docker, k8s)
    
    return pickle.loads(result_obj_str)

@task
def func_workers(ctx, user, func, local=False, docker=False, k8s=True):
    """
    List all warm Faasm instances
    """
    worker_set_name = "w_{}/{}".format(user, func)
    obj_str =_do_redis_command(
        "smembers {}".format(worker_set_name), "REDIS_QUEUE_HOST", local, docker, k8s
    )
