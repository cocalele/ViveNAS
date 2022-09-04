# Roadmap

This document provides information on ViveNAS development in current and upcoming releases. Community and contributor involvement is vital for successfully implementing all desired items for each release. We hope that the items listed below will inspire further engagement from the community to keep ViveNAS progressing and shipping exciting and valuable features.

ViveNAS follows a lean project management approach by splitting the development items into current, near term and future categories.

## Current

### ViveFS
 Implement all posix FS semantic, include:
 - support various file types:
   - File create/read/write/delete/...
   - Dir 
   - Symbol link
 - support user and group

### Service high availability
 Provide HA for ViveNAS:
 - deploy with container
 - failover with help of k8s
 

### FSAL_VIVENAS
 - clean code that was inherited from FSAL_MEM



## Near Term

Typically the items under this category fall under next major release (after the current. e.g 4.0). At a high level, the focus is towards moving the beta engines towards stable by adding more automated e2e tests and updating the corresponding user and contributor documents. To name a few backlogs (not in any particular order) on the near-term radar, where we are looking for additional help: 

### LsmTree (rocksdb)
 - Support to select media type for different LSM layer, with user defined policy
 - Performance optimization by adopt Key-Value separation or other novel method
 
### PfAof
 - Support to use different media, include NVMe-SSD, HDD, SMR-HDD, ZNS-SSD, Tape as under layer media. (With help of PureFlash)
 
## Future
As the name suggests this bucket contains items that are planned for future.
 - Support to use CXL memory pool as a distributed cache
 - Support Erasure Code on all type of medias
 - Quickly failover with help of sharable memory pool
 
# Getting involved with Contributions

We are always looking for more contributions. If you see anything above that you would love to work on, we welcome you to become a contributor and maintainer of the areas that you love. You can get started by commenting on the related issue or by creating a new issue.

#Release planning
 - v0.3 at 2022.12, all features in 'Current' stage will be included