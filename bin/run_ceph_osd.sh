#!/bin/bash

# Ceph Quincy: 17.2 series
# Follows https://docs.ceph.com/en/quincy/install/manual-deployment/

set -ou pipefail

# if [[ -e "/ceph/build" ]]
# then
#     cd /ceph/build
#     ninja install
# fi

NODE_CEPH_DIR="/usr/local/faasm/ceph-$(hostname)"
MON_CEPH_DIR="/usr/local/faasm/ceph-ceph-mon1"

mkdir -p "${NODE_CEPH_DIR}"
chown -R ceph:ceph /var/lib/ceph

while [[ ! -e "${MON_CEPH_DIR}/ceph.mon.keyring" ]]; do
    echo "Waiting for monitor keyring"
    sleep 1
done

echo "Received ceph.mon.keyring" >> /osd/osd_log

if [[ ! -e "/osd/osd_secret" ]]; then
    cp -a "${MON_CEPH_DIR}"/ceph.client.admin.keyring /etc/ceph/ceph.client.admin.keyring
    cp -a "${MON_CEPH_DIR}"/ceph.bootstrap-osd.keyring /var/lib/ceph/bootstrap-osd/ceph.keyring

    OSD_UUID="$(uuidgen -n @dns -s -N $(hostname)-osd)"
    OSD_SECRET="$(ceph-authtool --gen-print-key)"
    echo OSD UUID ${OSD_UUID}

    OSD_ID=$(echo "{\"cephx_secret\": \"$OSD_SECRET\"}" | ceph osd new $OSD_UUID -i - -n client.admin -k /etc/ceph/ceph.client.admin.keyring)

    echo "$OSD_UUID" > /osd/osd_uuid
    echo "$OSD_SECRET" > /osd/osd_secret
    echo "$OSD_ID" > /osd/osd_id

    echo "Initialized ${OSD_UUID} ${OSD_SECRET} ${OSD_ID}" >> /osd/osd_log

    ceph-authtool --create-keyring /var/lib/ceph/osd/ceph-$OSD_ID/keyring --name osd.$OSD_ID --add-key $OSD_SECRET
    echo "Created keyring /var/lib/ceph/osd/ceph-$OSD_ID/keyring" >> /osd/osd_log

    ceph-osd -i $OSD_ID --mkfs --osd-uuid $OSD_UUID
    echo "ceph-osd init" >> /osd/osd_log

    chown -R ceph:ceph /var/lib/ceph/osd/ceph-$OSD_ID
else
    OSD_UUID="$(cat /osd/osd_uuid)"
    OSD_SECRET="$(cat /osd/osd_secret)"
    OSD_ID="$(cat /osd/osd_id)"

    echo "Old vals ${OSD_UUID} ${OSD_SECRET} ${OSD_ID}" >> /osd/osd_log
fi

# Run ceph
export TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES=134217728
echo "exec ceph-osd ${OSD_ID}" >> /osd/osd_log
cat /osd/osd_log | tail -n 100

exec ceph-osd -f --cluster "ceph" --id "${OSD_ID}" --setuser ceph --setgroup ceph
