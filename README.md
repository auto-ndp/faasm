<div align="center">
<img src="https://raw.githubusercontent.com/faasm/faasm/main/faasm_logo.png"></img>
</div>

# AutoNDP-Faasm [![Tests](https://github.com/faasm/faasm/workflows/Tests/badge.svg?branch=main)](https://github.com/faasm/faasm/actions)  [![License](https://img.shields.io/github/license/faasm/faasm.svg)](https://github.com/faasm/faasm/blob/main/LICENSE.md)  [![Release](https://img.shields.io/github/release/faasm/faasm.svg)](https://github.com/faasm/faasm/releases/)  [![Contributors](https://img.shields.io/github/contributors/faasm/faasm.svg)](https://github.com/faasm/faasm/graphs/contributors/)


### Faasm

This work is built atop Faasm, a high-performance serverless runtime.

Faasm provides multi-tenant isolation, yet allows functions to share regions of
memory. These shared memory regions give low-latency concurrent access to data,
and are synchronised globally to support large-scale parallelism across multiple
hosts.

Faasm combines software fault isolation from WebAssembly with standard Linux
tooling, to provide security and resource isolation at low cost. Faasm runs
functions side-by-side as threads of a single runtime process, with low
overheads and fast boot times.

Faasm defines a custom host interface that extends [WASI](https://wasi.dev/) to
include function inputs and outputs, chaining functions, managing state,
accessing the distributed filesystem, dynamic linking, pthreads, OpenMP and MPI.

You can find the Usenix ATC '20 paper on Faasm [here](https://www.usenix.org/conference/atc20/presentation/shillaker).

Please see the [full documentation](https://faasm.readthedocs.io/en/latest/) for
more details on the code and architecture pertaining to Faasm.

### Near-Storage Offloading

Our work introduces an efficient migration mechanism for Serverless Functions across machines to move I/O-intensive parts of Functions to the relevant storage node, with minimal code changes. 
This allows Functions to perform arbitrary computations on storage nodes, benefiting from the locality of data, whilst maintaining compute-storage disaggregation.

## Cloudlab

Within the *autoNDP* project, instantiate the *test-offloading* profile. 
Pick a cluster with at least 10 GB/s ethernet speed, and 256 GB of disk storage.
Instantiate an experiement with 7 nodes (Nodes 0-2 are Storage Nodes, 3-5 are Compute Nodes, and Node 6 is the Load-Generator).
You may pick a different configuration, but ensure that you modify the `docker-compose-cloudlab.yml` file accordingly.

## Setup

First ssh into every node, and `sudo su` to become root user.
Now clone this repository, and `cd` into it.
Then run this on each node.

```bash
git clone --recurse-submodules --branch donald-ug https://github.com/auto-ndp/faasm.git && cd faasm && git submodule update --remote && source ./bin/one-click-setup.sh
```

Now build everything once for the first time
```bash
./bin/cli.sh faasm
# Now you have entered the faasm-cli container
(faasm-cli)$ ./bin/buildfaasm.sh # Build all binaries for the first time
(faasm-cli)$ exit
# Now back on host
./deploy/local/dev_cluster.sh # download and start all containers for the first time
docker compose down
```
We use *docker swarm* to manage all containers across the cluster.

On **Node 0** run the following command.
```bash
docker swarm init --advertise-addr 10.0.1.1
```

Copy the command (eg. `docker swarm join --token ABCDEFGHIJKLMNOP`) and run it on all the other nodes.
Nodes 1-6 should have joined the swarm as *workers* while Node 0 is the *manager*.
Now attach labels to the nodes. to identify them.

```bash
node0=$(docker node ls | grep node-0 | awk '{print $1;}')
node1=$(docker node ls | grep node-1 | awk '{print $1;}')
node2=$(docker node ls | grep node-2 | awk '{print $1;}')
node3=$(docker node ls | grep node-3 | awk '{print $1;}')
node4=$(docker node ls | grep node-4 | awk '{print $1;}')
node5=$(docker node ls | grep node-5 | awk '{print $1;}')
node6=$(docker node ls | grep node-6 | awk '{print $1;}')

docker node update --label-add rank=leader ${node0}
docker node update --label-add type=storage ${node0}
docker node update --label-add name=storage0 ${node0}

docker node update --label-add type=storage ${node1}
docker node update --label-add name=storage1 ${node1}

docker node update --label-add type=storage ${node2}
docker node update --label-add name=storage2 ${node2}

docker node update --label-add type=compute ${node3}
docker node update --label-add name=compute0 ${node3}

docker node update --label-add type=compute ${node4}
docker node update --label-add name=compute1 ${node4}

docker node update --label-add type=compute ${node5}
docker node update --label-add name=compute2 ${node5}

docker node update --label-add type=loadgen ${node6}
```

Inspect the node labels to see if they have been labelled correctly.
```bash
docker node ls -q | xargs docker node inspect \
  -f '{{ .ID }} [{{ .Description.Hostname }}]: {{ range $k, $v := .Spec.Labels }}{{ $k }}={{ $v }} {{end}}'
```
## Offloading

In the following commands, substitute `${FAASM_ROOT}` for the location where `faasm` is installed (eg. `/users/alannair/faasm`).

On **Node 0** (manager) run the following command to setup some necessary deps for the Ceph OSD (Object Storage Daemons) on every node in the swarm.
The `bin/runcmd.sh` script uses `ssh` to run commands on the worker nodes, so the first time it tries to connect to the workers, it will ask for confirmation for each worker.

```bash
./bin/runcmd.sh "cd ${FAASM_ROOT} && ./bin/setup-osd.sh setup"
```
Finally on **Node 0** do
```bash
docker stack deploy --compose-file docker-compose-cloudlab.yml faasm # deploy the swarm
./bin/setup-osd.sh sync # update the keys required by OSDs on all storage nodes
```

Check that all services are running well.
On Manager Node (**Node 0**) do `docker service ls`.
On each node, do `docker ps` to see the containers running on that node.

Rebuild the binaries (optional - do this if you have made modifications to the source code).
On **each node** do
```bash
docker exec -it $(docker ps | grep faasm_faasm-cli | awk '{print $1;}') /bin/bash # enter faasm-cli
(faasm-cli)$ ./bin/buildfaasm.sh
(faasm-cli)$ exit

docker exec -it $(docker ps | grep faasm_cpp | awk '{print $1;}') /bin/bash # enter faasm-cpp
(cpp)$ ./bin/buildcpp.sh
(cpp)$ exit
```

## Run Offloading Experiments

Now let us see offloading in action.
The `clients/cpp/func/ndp` directory contains some example functions that make use of offloading.
We shall look at `wordcount.cpp` as a representative example.

On the *loadgen* node (client node), first build and upload the functions bytecodes.
```bash
docker exec -it $(docker ps | grep faasm_cpp | awk '{print $1;}') /bin/bash # enter faasm-cpp

(cpp)$ inv func ndp get # build get.cpp
(cpp)$ inv func ndp put # build put.cpp
(cpp)$ inv func ndp wordcount # build wordcount.cpp

# upload the bytecodes (to the upload service running on Leader Node)
(cpp)$ inv func.upload ndp get
(cpp)$ inv func.upload ndp put
(cpp)$ inv func.upload ndp wordcount

# the following steps can be performed from either the faasm-cli or the cpp container on the loadgen node

# put a key-value object {'key': 'v1 v2 v3'} into the storage backend
(cpp)$ curl -X POST 'http://worker-0:8080/f/' -H "Content-Type: application/json" -d '{"async": false, "user": "ndp", "function": "put", "input_data": "key v1 v2 v3"}'

# get the same to test that it works
(cpp)$ curl -X POST 'http://worker-0:8080/f/' -H "Content-Type: application/json" -d '{"async": false, "user": "ndp", "function": "get", "input_data": "key"}'

# run wordcount with offloading
(cpp)$ curl -X POST 'http://worker-0:8080/f/' -H "Content-Type: application/json" -d '{"async": false, "user": "ndp", "function": "wordcount", "input_data": "key"}'

# run wordcount without offloading
(cpp)$ curl -X POST 'http://worker-0:8080/f/' -H "Content-Type: application/json" -d '{"async": false, "user": "ndp", "function": "wordcount", "input_data": "key", "forbid_ndp": true}'
```
In the above example we tested `worker-0`.
Similarly test `worker-1` and `worker-2` as well.

## Restarting the Swarm

### Simple Restart

First turn down the swarm. On **Node 0** do
```bash
docker stack rm faasm
./bin/runcmd.sh "cd ${FAASM_ROOT} && ./bin/setup-osd.sh clean"
```
Now redo all the steps in the **Ofloading** section above.

### Clean Restart

If you need a fresh restart, some more steps are needed.

First turn down the swarm. On **Node 0** do
```bash
docker stack rm faasm
./bin/runcmd.sh "cd ${FAASM_ROOT} && ./bin/setup-osd.sh clean"
```

Now on **each node** do
```bash
./bin/refresh_local.sh -d
```

Next, do vanilla setup (to setup some directory structures appropriately as needed)

```bash
./bin/cli.sh faasm
# Now you have entered the faasm-cli container
(faasm-cli)$ ./bin/buildfaasm.sh # Build all binaries for the first time
(faasm-cli)$ exit
# now back on host
./deploy/local/dev_cluster.sh # download and start all containers for the first time
docker compose down
```

Now redo all the steps in the **Ofloading** section above.

## Logging back in

If you log in to a node that already has a running swarm, you need to enter the working environment.
```bash
source ./bin/cluster_env.sh
source ./bin/workon.sh
```

This command updates some of your session variables.
In the earlier setup, this command was executed from with `bin/one-click-setup.sh`.
