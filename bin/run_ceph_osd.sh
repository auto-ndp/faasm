#!/bin/bash

# Ceph Quincy: 17.2 series
# Follows https://docs.ceph.com/en/quincy/install/manual-deployment/

set -ou pipefail

NODE_CEPH_DIR="/usr/local/faasm/ceph-$(hostname)"
MON_CEPH_DIR="/usr/local/faasm/ceph-ceph-mon1"

mkdir -p "${NODE_CEPH_DIR}"
chown -R ceph:ceph /var/lib/ceph

while [[ ! -e "${MON_CEPH_DIR}/ceph.mon.keyring" ]]; do
    echo "Waiting for monitor keyring"
    sleep 1
done

if [[ ! -e "/osd_secret" ]]; then
    cp -a "${MON_CEPH_DIR}"/ceph.client.admin.keyring /etc/ceph/ceph.client.admin.keyring
    cp -a "${MON_CEPH_DIR}"/ceph.bootstrap-osd.keyring /var/lib/ceph/bootstrap-osd/ceph.keyring

    OSD_UUID="$(uuidgen -n @dns -s -N $(hostname)-osd)"
    OSD_SECRET="$(ceph-authtool --gen-print-key)"
    echo OSD UUID ${OSD_UUID}
    echo CEPH-OSD1 "$(uuidgen -n @dns -s -N ceph-osd1-osd)"
    echo CEPH-OSD2 "$(uuidgen -n @dns -s -N ceph-osd2-osd)"
    echo CEPH-OSD3 "$(uuidgen -n @dns -s -N ceph-osd3-osd)"
    echo CEPH-OSD4 "$(uuidgen -n @dns -s -N ceph-osd4-osd)"
    echo CEPH-OSD5 "$(uuidgen -n @dns -s -N ceph-osd5-osd)"
    OSD_ID=$(echo "{\"cephx_secret\": \"$OSD_SECRET\"}" | ceph osd new $OSD_UUID -i - -n client.admin -k /etc/ceph/ceph.client.admin.keyring)

    echo "$OSD_UUID" > /osd_uuid
    echo "$OSD_SECRET" > /osd_secret
    echo "$OSD_ID" > /osd_id

    # mkdir -p /var/lib/ceph/osd/ceph-$OSD_ID
    # dd if=/dev/zero of=/ceph-$OSD_ID.img bs=1 count=0 seek=100G
    # mkfs.ext4 /ceph-$OSD_ID.img
    # mount -o user_xattr /ceph-$OSD_ID.img /var/lib/ceph/osd/ceph-$OSD_ID
    # mount /dev/loop2${OSD_ID} /var/lib/ceph/osd/ceph-$OSD_ID

    ceph-authtool --create-keyring /var/lib/ceph/osd/ceph-$OSD_ID/keyring --name osd.$OSD_ID --add-key $OSD_SECRET
    ceph-osd -i $OSD_ID --mkfs --osd-uuid $OSD_UUID
    chown -R ceph:ceph /var/lib/ceph/osd/ceph-$OSD_ID
else
    OSD_UUID="$(cat /osd_uuid)"
    OSD_SECRET="$(cat /osd_secret)"
    OSD_ID="$(cat /osd_id)"
fi

# # Run ceph
export TCMALLOC_MAX_TOTAL_THREAD_CACHE_BYTES=134217728
exec /usr/bin/ceph-osd -f --cluster "ceph" --id "${OSD_ID}" --setuser ceph --setgroup ceph
