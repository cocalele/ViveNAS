FROM pureflash/pfs-dev:1.6
LABEL version="1.8"

#ARG BUILD_BIN

RUN rm -rf /tmp/zookeeper
RUN mkdir /tmp/zookeeper
RUN apt install -y liburcu-dev  bison flex libgflags-dev 

COPY bin/libganesha_nfsd.so.4.0 /opt/pureflash
COPY bin/libntirpc.so.4.0 /opt/pureflash
COPY bin/libvivenas.so /opt/pureflash
COPY bin/ganesha.nfsd /opt/pureflash
COPY bin/mkfs.vn /opt/pureflash
COPY bin/libfsalmem.so.4.2.0 /opt/pureflash
COPY bin/scripts/run-vn.sh /opt/pureflash

RUN ln -s /opt/pureflash/libganesha_nfsd.so.4.0 libganesha_nfsd.so
RUN ln -s /opt/pureflash/libfsalmem.so.4.2.0 libfsalmem.so.4
RUN ln -s /opt/pureflash/libfsalmem.so.4.2.0 libfsalmem.so
RUN ln -s /opt/pureflash/libntirpc.so.4.0 libntirpc.so

#ENTRYPOINT 
CMD [ "/opt/pureflash/run-vn.sh" ]

