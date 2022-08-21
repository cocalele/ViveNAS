
cmake_minimum_required(VERSION 3.1)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0015 NEW)
endif()

project(ViveNAS)

execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/osname.sh   OUTPUT_VARIABLE  OS_NAME)
message("os is ${OS_NAME}")
set(CMAKE_BUILD_DIR ${CMAKE_BINARY_DIR})
set(CMAKE_BINARY_DIR ${CMAKE_BINARY_DIR}/bin)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR})
set(THIRDPARTY ${CMAKE_CURRENT_SOURCE_DIR}/thirdParty)
set(GANESHA_SRC ./nfs-ganesha)

set (CMAKE_CXX_STANDARD 17)

execute_process(COMMAND git submodule update --init
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
				
#compile PureFlash
execute_process(COMMAND git submodule update --init
				COMMAND mkdir build
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/PureFlash
				) #COMMAND_ERROR_IS_FATAL  ANY not supported 
#execute_process(COMMAND ant compile_jute
#                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/PureFlash/thirdParty/zookeeper
#				)
#execute_process(COMMAND autoreconf -if
#				COMMAND ./configure --enable-debug --without-cppunit
#				COMMAND make -j4
#                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/PureFlash/thirdParty/zookeeper/zookeeper-client/zookeeper-client-c
#				)
#execute_process(COMMAND cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug
#                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/PureFlash/build
#				)

#compile rocksdb
execute_process(COMMAND git submodule update --init
				COMMAND mkdir build
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb
				)


#execute_process(COMMAND cmake .. -GNinja -DCMAKE_BUILD_TYPE=Debug -DUSE_RTTI=1
#				COMMAND ninja
#                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb/build
#				)

execute_process(COMMAND git submodule update --init
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/nfs-ganesha
				)				
execute_process(COMMAND  sed -i s/CMAKE_SOURCE_DIR/CMAKE_CURRENT_SOURCE_DIR/g CMakeLists.txt
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/nfs-ganesha/src/libntirpc
				)		
set(USE_GSS OFF CACHE BOOL "disable GSS" FORCE)
option(USE_GSS OFF)
set(VIVENAS_HOME ${CMAKE_CURRENT_SOURCE_DIR})
set(FSAL_DESTINATION ${CMAKE_BINARY_DIR})
add_subdirectory(nfs-ganesha/src)


add_compile_options (-fdiagnostics-color=always)

set(CMAKE_C_FLAGS   "-Wall -Wno-conversion -Wno-sign-compare -std=c99 -fPIC -fms-extensions -Wno-variadic-macros -Wno-format-truncation -I/usr/include ${C_FLAG_GCOV}")
set(CMAKE_CXX_FLAGS   "-Wall -Wconversion -Wno-sign-compare  -std=c++17 -fPIC -fms-extensions -Wno-variadic-macros -Wno-format-truncation -I/usr/include -D_XOPEN_SOURCE ${C_FLAG_GCOV}")
set(CMAKE_C_FLAGS_DEBUG  "-O0 -g -DDEBUG ")
set(CMAKE_CXX_FLAGS_DEBUG  "-O0 -g -DDEBUG ")
add_definitions(-D_XOPEN_SOURCE)

INCLUDE_DIRECTORIES(include  rocksdb rocksdb/include PureFlash/common/include PureFlash/pfs/nlohmann_json/include 
                    ${GANESHA_SRC}/src/include ${GANESHA_SRC}/build/include ${GANESHA_SRC}/src/libntirpc/ntirpc)
#add_subdirectory(FSAL_MEM)

set (VIVE_SRC 
	src/pfaof.cpp
	src/main.cpp
    src/file.cpp
    src/data_merge.cpp
 #   src/fsal/vn_registry.cpp 
 #   src/fsal/vn_handle.cpp
 #   src/fsal/vn_export.cpp
 #   src/fsal/vn_up.cpp
 )
 set (MKFS_SRC 
	src/pfaof.cpp
    src/mkfs.cpp
    src/main.cpp
    src/file.cpp
    src/data_merge.cpp
)
set(ZOOKEEPER_SOURCE PureFlash/thirdParty/zookeeper/zookeeper-client/zookeeper-client-c)

#link_directories(./PureFlash/build/bin ./rocksdb ${ZOOKEEPER_SOURCE}/.libs)
link_directories( ./pre_build_libs/${OS_NAME})

add_library(vivenas  SHARED ${VIVE_SRC} ${INCS})
add_executable(mkfs.vn  ${MKFS_SRC} ${HTTP_SRC} ${INCS})

TARGET_LINK_LIBRARIES(vivenas pthread uuid s5common  curl  rocksdb zookeeper_mt rdmacm ibverbs stdc++)
TARGET_LINK_LIBRARIES(mkfs.vn pthread uuid s5common  curl  rocksdb zookeeper_mt rdmacm ibverbs )
add_custom_target(scripts ALL
	COMMAND cp -rpfu ${CMAKE_CURRENT_SOURCE_DIR}/scripts ${CMAKE_BINARY_DIR}
	COMMAND cp -rpfu ${CMAKE_CURRENT_SOURCE_DIR}/docker/Dockerfile ${CMAKE_BUILD_DIR}
	#COMMAND mv -f ${CMAKE_BINARY_DIR}/scripts/env.sh ${CMAKE_BINARY_DIR}/..
)

#add_custom_target( all COMMAND ROCKSDB_PLUGINS="pfaof" PF_INC=~/v2/PureFlash/common/include PF_LIB=~/v2/PureFlash/build_deb/bin make simple_example
#                   COMMAND date
#                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
#)
#
#
#
#simple_example: librocksdb simple_example.cc
#	$(CXX) $(CXXFLAGS) -I$(PF_INC)  $@.cc -o$@ ../librocksdb.a -I../include -O0 -g -std=c++11 $(PLATFORM_LDFLAGS) $(PLATFORM_CXXFLAGS) $(EXEC_LDFLAGS) -L$(PF_LIB) -L/root/v2/PureFlash/thirdParty/zookeeper/zookeeper-client/zookeeper-client-c  -ls5common -luring  -lrdmacm -libverbs -lpthread -luuid -lzookeeper -lcurl -lhashtable
	