# ViveNAS

## What's ViveNAS
ViveNAS is Network Attached Filesystem(NAS), which provide NFS  service currently.

Aim of ViveNAS is to provide a NAS storage that has widely media adaption so we can store long term data at very cheap cost.
 And when the data is accessed occasionally it can be activated quickly and provide a very high performance.
 
Characters of ViveNAS:
 - Pursue a dynamic balance between performance and cost via combining different storage media
 - Solve the problems of long term data store, support media like tape, SMR HDD and EC algorithm
 - Get ready for CXL memory pooling and SCM technology, whenever they are ready ViveNAS can leverage them to provide outstanding performance
 - Solve the problem of small file storage
 - Provide a controlled distribution policy for enterprise storage. So it can solve problems like scaling, balancing, recovering, etc.
 - A green storage, that can make a full utilize to resource like RAM, CPU which are over-provisioned in modern datacenter.


ViveNAS dependents on two core technology to provide above ability:
__Core tech 1__，the PureFlash Server SAN system

   PureFlash provide all features that are related with distribution system, include HA, fault tolerance, snapshot, clone. 

   PureFlahs is a distributed ServerSAN storage system with it's design philosophy from fully FPGA implemented all-flash system.  So PureFlash has a very simple IO stack.
   
   Differ to other distributed storage system which based on hash algorithm, distribution of data in PureFlash is totally controllable. This provide the stability for enterprise storage, for the "human being" rather than a "machine" have the final decision.  (Refer github.com/cocalele/PureFlash for more)

   PureFlash support to manage different media in one cluster, include NVMe SSD, HDD, tape and support access as AOF file.

   All the above features give a solid support to ViveNAS.

__Core tech 2__，SLM tree based VIVEFS

   ViveFS is a userspace filesystem based on LSM tree. LSM tree have two major characters: store in multiple levels; sequential write only in each level.
   
   ViveFS put level 0 into DRAM or CXL memory pool, while the other levels will be put into different medias provided by PureFlash. All level data are highly available.


   The second benefit of LSM tree, i.e. sequential write, make it very suitable for SMR HDD and tape. So ViveNAS can put cold data into cheap media for long term store.
   This is one of major aim of ViveNAS. Also sequential write is very friendly to EC algorithm, with which can make cost lower.
  
## architecture
    +-------------------+
    |Ganesha-NAS portal |
    +-------------------+
             |
             |
    +--------v----------+
    |ViveNAS FSAL       |
    +-------------------+
             |
             |
    +--------v----------+
    | LSM K-V (rocksdb) |
    +-------------------+
             |
             |
    +--------v----------+
    | PureFlash (AOF)   |
    +-------------------+
             |
             |
    +--------v----------+
    | Multiple Medias   |
    +-------------------+
 


# Build and run
## setup build environment from scratch
  0) follow the guides in PureFlash/build_and_run.txt to setup a compile environment for PureFlash
  1) For ubuntu, run following command to install additional dependency:
```
  # apt install liburcu-dev  bison flex libgflags-dev  libblkid-dev libzstd-dev 
  # to run rocksdb db_bench, also install:
  # apt install time bc
```
  To simplify the compiling process, some thirdparty libraryies are prebuild into binary. For now only ubuntu20.04 is supported.

## use the container for build
   It may be a bit complicate to setup build environment from scratch since ViveNAS/PureFlash use many third party libraries. I strongly suggest you to use the container for build
   ```
   # docker pull pureflash/vivenas-dev:1.9
   ```
   All the dependencies and build tools have already deployed in this container.
  2） clone code
```
  # git clone https://github.com/cocalele/ViveNAS.git
```
  3) build
```
  # cd ViveNAS; VIVENAS_HOME=$(pwd)
  # git submodule update --init --recursive
  # cd rocksdb
  # mkdir build; cd build
  # cmake -S .. -GNinja -DCMAKE_BUILD_TYPE=Release -DUSE_RTTI=1 -DWITH_ZSTD=ON -B . -DROCKSDB_PLUGINS=pfaof -DPF_INC=/root/ViveNAS/PureFlash/common/include/ -DPF_LIB=/root/ViveNAS/PureFlash/build/bin
  # ninja
  
  # cd $VIVENAS_HOME/PureFlash/
  # git submodule update --init --recursive
  # mkdir build; cd build
  # cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ..
  # ninja

  # cd $VIVENAS_HOME
  # mkdir build; cd build
  # cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug
  # ninja
```
  4) Run
  run ganesha:
```
#  mkdir -p /var/lib/nfs/ganesha  /var/run/ganesha /usr/lib/ganesha
# apt install liburcu6
# apt-get install libgflags-dev 
# ln -s /root/v2/ViveNAS/out/build/Linux-GCC-Debug/bin/libfsalvivenas.so /usr/lib/ganesha/libfsalvivenas.so
# export LD_LIBRARY_PATH=/root/v2/nfs-ganesha/src/libntirpc/src:$LD_LIBRARY_PATH
# mkfs.vn /vivenas_a
# LD_PRELOAD=/usr/lib/x86_64-linux-gnu/librdmacm.so.1.2.28.0 ../nfs-ganesha/build/ganesha.nfsd -F  -f ./ganesha-vivenas.conf -L /dev/stderr -p /var/run/ganesha.pid
```

## Performance estimation 
accord to rocksdb benchmark, https://github.com/facebook/rocksdb/wiki/Performance-Benchmarks