# ViveNAS

## What's ViveNAS
ViveNAS is Network Attached Filesystem, which provide NFS, CIFS service.

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
 