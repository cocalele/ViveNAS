FROM pureflash/pureflash:1.8.1
LABEL version="1.9.1"

#ARG BUILD_BIN

RUN rm -rf /tmp/zookeeper
RUN mkdir -p /tmp/zookeeper /var/crash
RUN apt install -y liburcu-dev  bison flex libgflags-dev nfs-common

COPY bin/libganesha_nfsd.so.4.0 /opt/pureflash
COPY bin/libntirpc.so.4.0 /opt/pureflash
COPY bin/libvivenas.so /opt/pureflash
COPY bin/librocksdb.so.6.29.0 /opt/pureflash
COPY bin/ganesha.nfsd /opt/pureflash
COPY bin/mkfs.vn /opt/pureflash
COPY bin/libfsalvivenas.so.1.0.0 /opt/pureflash
COPY docker/run-vn.sh /opt/pureflash
COPY ganesha-vivenas.conf /etc/

RUN ln -s /opt/pureflash/libganesha_nfsd.so.4.0 /opt/pureflash/libganesha_nfsd.so
RUN ln -s /opt/pureflash/libfsalvivenas.so.1.0.0 /opt/pureflash/libfsalvivenas.so.1
RUN ln -s /opt/pureflash/libfsalvivenas.so.1.0.0 /opt/pureflash/libfsalvivenas.so
RUN ln -s /opt/pureflash/libntirpc.so.4.0 /opt/pureflash/libntirpc.so
RUN ln -s /opt/pureflash/librocksdb.so.6.29.0 /opt/pureflash/librocksdb.so
RUN ln -s /opt/pureflash/librocksdb.so.6.29.0 /opt/pureflash/librocksdb.so.6
RUN ln -s /opt/pureflash/librocksdb.so.6.29.0 /opt/pureflash/librocksdb.so.6.29
RUN ln -s /opt/pureflash/librocksdb.so.6.29.0 /usr/lib64/librocksdb.so.6.29
RUN mkdir /var/run/ganesha
#ENTRYPOINT 
CMD [ "/opt/pureflash/run-vn.sh" ]

