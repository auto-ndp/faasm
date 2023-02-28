import os
import sys

NUM_OSD = 5

def get_cur_fsid(id):
  for i in range(NUM_OSD):
    curid = open('/mnt/ceph' + str(i) + '/fsid', 'r').read()[:-1]
    if curid == id:
      return i
  sys.exit("ERROR get_cur_fsid returns -1")
  return -1

def create_mapping():
  curdir = os.path.dirname(os.path.abspath(__file__))
  rootdir = os.path.abspath(os.path.join(curdir, os.pardir))
  files = [os.path.join(rootdir, '.fsid.' + str(i)) for i in range(NUM_OSD)]
  fsid = [open(f, 'r').read()[:-1] for f in files]

  mapping = []
  for i in range(NUM_OSD):
    mapping.append(get_cur_fsid(fsid[i]))

  return mapping

def do_mounts_from_mapping(mapping):
  for i in range(NUM_OSD):
    os.system('umount /mnt/ceph' + str(i))
  
  for i in range(NUM_OSD):
    os.system('mount -o user_xattr /dev/loop3' + str(i) + ' /mnt/ceph' + str(mapping[i]))


mapping = create_mapping()
do_mounts_from_mapping(mapping)