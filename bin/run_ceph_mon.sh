#!/bin/bash

# Ceph Quincy: 17.2 series
# Follows https://docs.ceph.com/en/quincy/install/manual-deployment/

set -eou pipefail

NODE_CEPH_DIR="/usr/local/faasm/ceph-$(hostname)"

mkdir -p "${NODE_CEPH_DIR}"

chown -R ceph:ceph /var/lib/ceph

if [[ ! -e "${NODE_CEPH_DIR}/ceph.mon.keyring" ]]; then
    ceph-authtool --create-keyring "${NODE_CEPH_DIR}"/ceph.mon.keyring --gen-key -n mon. --cap mon 'allow *'
    ceph-authtool --create-keyring "${NODE_CEPH_DIR}"/ceph.client.admin.keyring --gen-key -n client.admin --cap mon 'allow *' --cap osd 'allow *' --cap mds 'allow *' --cap mgr 'allow *'
    ceph-authtool --create-keyring "${NODE_CEPH_DIR}"/ceph.bootstrap-osd.keyring --gen-key -n client.bootstrap-osd --cap mon 'profile bootstrap-osd' --cap mgr 'allow r'

    ceph-authtool "${NODE_CEPH_DIR}"/ceph.mon.keyring --import-keyring "${NODE_CEPH_DIR}"/ceph.client.admin.keyring
    ceph-authtool "${NODE_CEPH_DIR}"/ceph.mon.keyring --import-keyring "${NODE_CEPH_DIR}"/ceph.bootstrap-osd.keyring

    chown ceph:ceph "${NODE_CEPH_DIR}"/ceph.mon.keyring
fi

ln -sf "${NODE_CEPH_DIR}"/ceph.client.admin.keyring /etc/ceph/ceph.client.admin.keyring
ln -sf "${NODE_CEPH_DIR}"/ceph.bootstrap-osd.keyring /var/lib/ceph/bootstrap-osd/ceph.keyring

rm -f "${NODE_CEPH_DIR}"/monmap
monmaptool --create --add "$(hostname -s)" "$(hostname -i)" --fsid "94ccae01-cc84-4fc0-96fd-032579767ef6" "${NODE_CEPH_DIR}"/monmap
sudo -u ceph mkdir -p /var/lib/ceph/mon/ceph-$(hostname -s)

sudo -u ceph ceph-mon --cluster "ceph" --mkfs -i "$(hostname -s)" --monmap "${NODE_CEPH_DIR}"/monmap --keyring "${NODE_CEPH_DIR}"/ceph.mon.keyring

# Run ceph
export TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES=134217728
/usr/bin/ceph-mon -f --cluster "ceph" --id "$(hostname -s)" --setuser ceph --setgroup ceph &
sleep 1;
mkdir -p /var/lib/ceph/mgr/"ceph-$(hostname -s)"
ceph auth get-or-create mgr."$(hostname -s)" mon 'allow profile mgr' osd 'allow *' mds 'allow *' > /var/lib/ceph/mgr/"ceph-$(hostname -s)"/keyring
chown -R ceph:ceph /var/lib/ceph/mgr/"ceph-$(hostname -s)"/
exec /usr/bin/ceph-mgr -f --cluster "ceph" --id "$(hostname -s)" --setuser ceph --setgroup ceph

