#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh
VN_BUILD_DIR=$( cd $DIR/.. && pwd)
info "Use build dir:$VN_BUILD_DIR"

assert  which mount.nfs
assert_equal "$(pidof ganesha.nfsd)" ""

#docker run -ti  --ulimit core=-1 --privileged --hostname pfs-d  --net pfnet --ip 172.1.1.2  --rm -v /root/pf-2/etc-pureflash:/etc/pureflash -v /root/pf-2/opt-pureflash:/opt/pureflash  -v /root/v2:/root/v2   --name pfs-d  -e TZ=Asia/Shanghai vivenas-dev:1.7 /bin/bash
#ln -s /root/v2/nfs-ganesha/build/FSAL/FSAL_MEM/libfsalmem.so /usr/lib/ganesha/libfsalmem.so
mkdir -p /var/lib/nfs/ganesha  /var/run/ganesha /usr/lib/ganesha

for v in $(pfcli list_volume |awk -F\| '{print $3}' |tail -n +5); do pfcli delete_volume -v $v; done

export PATH=$VN_BUILD_DIR/bin:$PATH
ln -s $VN_BUILD_DIR/bin/libfsalvivenas.so.1.0.0 /opt/pureflash/libfsalvivenas.so
mkfs.vn /vivenas_a

rm -f vn2.log vn.log
info "start nfsd"
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/librdmacm.so.1.2.28.0 ganesha.nfsd -p `pwd`/nfsd.pid -F  -f $VN_BUILD_DIR/ganesha-vivenas.conf -L vn2.log &> vn.log &
NFSD_PID=$!
sleep 2
assert_proc $NFSD_PID

echo -n "Wait ganesha.nfsd ready ..."
while ! grep "NFS SERVER INITIALIZED" vn2.log; do sleep 1; echo -n "."; done
echo "Started"

MNT_DIR="./mnt"
info "Mount /some to $MNT_DIR"
mkdir $MNT_DIR
assert mount.nfs localhost:/some $MNT_DIR
assert mkdir $MNT_DIR/dd
str1="Hello World"
echo $str1 > $MNT_DIR/f1.txt
assert_equal $? 0

str2="How are you"
echo $str2 > $MNT_DIR/dd/f2.txt
assert_equal $? 0

info "Now umount and shutdown"
assert umount $MNT_DIR
kill -s 2 $NFSD_PID
assert wait $NFSD_PID

info "Restart nfsd"
rm -f vn.log vn2.log
LD_PRELOAD=/usr/lib/x86_64-linux-gnu/librdmacm.so.1.2.28.0 ganesha.nfsd -p `pwd`/nfsd.pid -F  -f $VN_BUILD_DIR/ganesha-vivenas.conf -L vn2.log &> vn.log &
NFSD_PID=$!
sleep 2
assert_proc $NFSD_PID

echo -n "Wait ganesha.nfsd ready ..."
while ! grep "NFS SERVER INITIALIZED" vn2.log; do sleep 1; echo -n "."; done
echo "Restarted"

assert mount localhost:/some $MNT_DIR
if [[ ! -d $MNT_DIR/dd ]]; then
	fatal "$MNT_DIR/dd lost"
fi
assert_equal "$str1" "$(cat $MNT_DIR/f1.txt)"
assert_equal "$str2" "$(cat $MNT_DIR/dd/f2.txt)"

info "Now umount and shutdown"
assert umount $MNT_DIR
kill -s 2 $NFSD_PID
assert wait $NFSD_PID

info "Test OK"