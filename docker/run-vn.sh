#!/bin/bash
set -m

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

#start PureFlash
export NOBASH="1"
source  /opt/pureflash/run-all.sh
export LD_LIBRARY_PATH=/opt/pureflash/:$LD_LIBRARY_PATH
echo " 1) format a vivenas FS "
mkfs.vn  /vivenas_a
echo "2) start NFS server ..."
ganesha.nfsd -F  -f /etc/ganesha-vivenas.conf -L /dev/stderr
bash

