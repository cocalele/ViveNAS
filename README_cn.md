# ViveNAS

## ViveNAS是什么
ViveNAS 是一个分布式的网络文件系统（NAS）, 目前版本提供NFS服务。

ViveNAS的特点:
 - 通过不同存储介质的结合，在高性能、低成本间寻找动态的平衡
 - 解决数据的长期、低成本存储问题，支持磁带，SMR HDD等低成本介质，以及EC
 - 为CXL内存池、SCM等新技术的应用做好准备，并应用这些新技术产生澎湃性能，服务热点数据
 - 解决小文件存储难题
 - 为企业存储提供受控的分布式策略，以解决传统分布式存储在扩容、均衡、故障恢复时面临的各种难题
 - 绿色存储，充分利用数据中心超配而不能充分利用的内存、CPU资源提供服务，降低能源消耗

ViveNAS提供上述能力的核心技术依赖于下列两项：
核心1，PureFlash 分布式SAN存储
   PureFlash 提供了我们这个存储系统所有跟分布式有关的特性，包括高可用机制、故障恢复机制、存储虚拟化、快照、克隆等。
   PureFlash是一个分布式的ServerSAN存储系统，他的核心思想继承自NetBRIC S5，一个以全FPGA硬件实现的全闪存储系统，因此PureFlash拥有一个极度简单的IO栈，最小的IO开销。
   区别与以hash算法为基础的分布式系统，PureFlash的数据分布是完全人为可控的，这提供了企业存储在运行时所需的稳定能力，因为数据分布的掌控权最终在“人”而不在“机器”。更多细节请参看github.com/cocalele/PureFlash
   PureFlash支持在一个集群里管理不同的存储质，包括从NVMe SSD、HDD、磁带，以及AOF文件访问，
   上述的这些为ViveNAS提供了坚实的数据存储保障。
核心2，以LSM tree为基础的ViveFS
   LSM tree有两个重要特点，一是多层级，二是每个层级都是顺序写。
   ViveFS将level 0 放在内存或者CXL内存池里，将其他层级放在PureFlash提供的不同存储介质里，而且每个层级都是分布式且具有高可靠性。
   顺序写这个特性对磁带/smr hdd介质非常的友好，这样ViveNAS就可以将比较低层级的数据放到这些低成本介质上。同时顺序写的AOF文件对ec也是很友好的，通过大因子EC可以进一步降低存储成本。
   



## architecture
    +-------------------+
    |Ganesha-NAS portal |
    +-------------------+
             |
             |
    +--------v----------+
    |ViveNAS & FSAL     |
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
## 使用容器编译环境
   作为一个分布式文件存储系统，ViveNAS依赖很多模块，因此编译环境的搭建其实还是比较复杂的。因此作者强烈建议你使用基于容器的编译环境。
   ```
   # docker pull pureflash/vivenas-dev:1.9
   ```
   这个容器里已经部署好了所有依赖项以及编译工具。
## 从头开始搭建编译环境
  0) follow the guides in PureFlash/build_and_run.txt to setup a compile environment or PureFlash
  1) For ubuntu, run following command:
```
  # apt install liburcu-dev  bison flex libgflags-dev  libblkid-dev
```
  To simplify the compilling process, some thirdparty libraryies are prebuild into binary. For now only ubuntu20.04 is supported.
  2） clone code
```
  # git clone https://github.com/cocalele/ViveNAS.git
```
  3) build
```
  # mkdir build; cd build
  # cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug
```
