"""
CloudLab Cluster Profile for all nodes in the [auto-ndp/faasm](https://github.com/auto-ndp/faasm) experiments.

Grows RootFS to maximum so as to be able to store data + containers. Script from USTIUGOV Dmitrii.

Nodes use Ubuntu 20.04 64-bit image.

Instructions:
Wait for the profile instance to start, then click on the node in the topology and choose the `shell` menu item.
"""

# Import the Portal object.
import geni.portal as portal
# Import the ProtoGENI library.
import geni.rspec.pg as pg

# Create a portal context, needed to defined parameters
pc = portal.Context()

# Create a Request object to start building the RSpec.
request = pc.makeRequestRSpec()

# Params
pc.defineParameter("node_type", "Hardware specs of the nodes to use (tested on xl170 on Utah, rs440 on Mass, m400 on OneLab).",
    portal.ParameterType.NODETYPE, "xl170", advanced=False, groupId=None)
pc.defineParameter("num_nodes", "Number of nodes to use.",
 portal.ParameterType.INTEGER, 2, legalValues=[], advanced=False, groupId=None)
params = pc.bindParameters()
if params.num_nodes < 1:
    pc.reportError(portal.ParameterError("You must choose a minimum of 1 node "))
pc.verifyParameters()

##
nodes = []
interfaces = []
blockstores = []

link = pg.LAN("link-0")

for i in range(params.num_nodes):
    nodes.append(pg.RawPC("node-%s" % i))
    nodes[i].hardware_type = params.node_type
    nodes[i].disk_image='urn:publicid:IDN+emulab.net+image+emulab-ops//UBUNTU20-64-STD'
    
    # Grow the root fs to the maximum. Note that saving the image is not possible with that!
    nodes[i].addService(pg.Execute(shell="bash", command="curl -s https://gist.githubusercontent.com/ustiugov/e38be6bca34a87a10cf0f68e04dd25a3/raw/8989892f62e500640da53e65a87aa0683119b763/cloudlab-grow-rootfs.sh | sudo bash"))

    interfaces.append(nodes[i].addInterface("interface-%s" % i))
    ip = "10.0.1." + str(i+1)
    interfaces[i].addAddress(pg.IPv4Address(ip, "255.255.255.0"))
    link.addInterface(interfaces[i])
    request.addResource(nodes[i])

request.addResource(link)


# Print the RSpec to the enclosing page.
pc.printRequestRSpec(request)