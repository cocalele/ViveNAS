#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/utils.sh


NFSCLIENT=127.0.0.1
NFSSERVER=127.0.0.1
RUN_CLIENT="ssh -p 6636 root@$NFSCLIENT"
export PATH=/opt/pureflash:$PATH

LOOP_CNT=0
declare -A RD_IOPS
declare -A WR_IOPS
declare -A RD_wr_BYTES
declare -A RD_rd_BYTES
declare -A WR_wr_BYTES
declare -A WR_rd_BYTES

function stat_pfs_io(){
	echo -e $(curl "http://localhost:49181/debug?op=disp_io" | jq '."perf_stat"')|tr -d '"' | sed 's/^ //g' | awk -F'[: ]' 'BEGIN{RD=0; WR=0} {RD=RD+$11; WR=WR+$9 } END{print  RD " " WR}' 
	
}

function stat_fs_iops(){
grep iops $1 | awk -F[=,] '{sum+=$6}END{printf "%d",  sum;}'
}


info "Kill old pfs and ganesha, 8s"
pkill ganesha.nfsd
PFSID=$(pidof pfs)
if [ "$PFSID" != "" ] ; then
	pkill -SIGINT pfs
	#wait $PFSID
fi
sleep 8

/opt/pureflash/restart-pfs.sh

echo -n "Waiting pfs ready"
sleep 5
while ! pfcli list_disk | grep OK ; do
	sleep 1
	echo -n "."
done
echo "Disk ready"
for f in $(pfcli list_volume | tail -n +5 | awk -F\| '{print $3}' ); do pfcli delete_volume -v $f; done

 
assert mkfs.vn /vivenas_a

info "Restart nfsd"
rm -f vn.log vn2.log
ganesha.nfsd -F  -f ./ganesha-vivenas.conf  -p /var/run/ganesha.pid -L vn2.log &> vn.log &
NFSD_PID=$!
sleep 2
assert_proc $NFSD_PID
echo -n "Wait ganesha.nfsd ready ..."
while ! grep "NFS SERVER INITIALIZED" vn2.log; do sleep 1; echo -n "."; done
echo "Restarted"



$RUN_CLIENT mkdir test
assert $RUN_CLIENT mount $NFSSERVER:/some test

FIOLOG="fio_wr_$LOOP_CNT.log"
assert $RUN_CLIENT  fio -filename=./test/file_fio.dat -size=4G -direct=1 -iodepth=2 -thread -rw=randwrite  -ioengine=libaio -bs=4K -numjobs=2 -runtime=30 -group_reporting -name=randw0 > $FIOLOG

WR_IOPS[$LOOP_CNT]=$(stat_fs_iops $FIOLOG)

info "Wait 30s for compaction"
sleep 60
read WR_rd_BYTES[$LOOP_CNT] WR_wr_BYTES[$LOOP_CNT] <<< $(stat_pfs_io )

FIOLOG="fio_rd_$LOOP_CNT.log"
assert $RUN_CLIENT  fio -filename=./test/file_fio.dat -size=4G -direct=0 -iodepth=2 -thread -rw=randread  -ioengine=psync -bs=16K -numjobs=24 -runtime=30 -group_reporting -name=randw0 > $FIOLOG
RD_IOPS[$LOOP_CNT]=$(stat_fs_iops $FIOLOG)
#curl "http://localhost:49181/debug?op=perf" | jq 
read RD_rd_BYTES[$LOOP_CNT] RD_wr_BYTES[$LOOP_CNT] <<< $(stat_pfs_io )
 
#curl "http://localhost:49181/debug?op=get_obj_count"
 
 
assert $RUN_CLIENT   umount test
kill  $NFSD_PID
assert wait $NFSD_PID
LOOP_CNT=$((LOOP_CNT+1))





echo "Write_iops          pfs_wr_GB(Write)    pfs_rd_GB(Write)    Read_iops           pfs_wr_GB(Read)     pfs_rd_GB(Read)" 
for ((i=0;i<$LOOP_CNT;i++)); do
	printf "%-20d%-20d%-20d%-20d%-20d%-20d\n" ${WR_IOPS[$i]}    $((${WR_wr_BYTES[$i]}>>30)) $((${WR_rd_BYTES[$i]}>>30)) ${RD_IOPS[$i]} $((${RD_wr_BYTES[$i]}>>30)) $((${RD_rd_BYTES[$i]}>>30))
done	