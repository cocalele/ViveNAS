# ViveNAS

## What's ViveNAS
ViveNAS is Network Attached Filesystem, which provide NFS  service currently.

Goal of ViveNAS is to provide the following ability:
  - scalability, just like server SAN architecture, ViveNAS provide unlimited scalability when servers increase
  - widely media adaption, ViveNAS can run on medias from PMEM, NVMe SSD, SATA/SAS SSD/HDD, ZNS SSD, SMR HDD, and tape system. 
  - reliability, ViveNAS can provide reliability via replicas and EC technology. 

  
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
## setup the build environment
  0) follow the guides in PureFlash/build_and_run.txt to setup a compile environment or PureFlash
  1) For ubuntu, run following command:
```
 apt install liburcu-dev  bison flex libgflags-dev  libblkid-dev
```
