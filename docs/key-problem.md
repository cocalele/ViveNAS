## 关键问题的解决方法

### 一，文件内容的组织
文件内容在rocksdb 里很多个k-v存在，每个k-v可以认为是一个extent, 规定一个extent 的大小为64KB。
key的格式如下：
由16Byte的binary构成，可以这么看
```
struct pfs_key{
     union { 
          struct {
               _le64 extent_index;
               _le64 inode_no;
          };
          char keybuf[16];
      }
}
```
每个文件的第一extent 除了文件数据外，在头部还包括inode数据。即首先是固定大小的inode 加上其次的extent 数据。
其他的extent 会包含16字节的头。

组成文件的第一个K-V , 
```
Key := (inode_no<<64)+0;
Value := <inode:256Byte> + <extent_head> + <data：64KByte>
```
其他的K-V,
```
Key := (inode_no<<64) +<extent_index>
Value := <extent_head> + <data：32KByte>
```

Write 的实现：
write操作使用rocksdb的merge操作实现，而不是read-modify-write这样的步骤。对一个extent的写入就是一个Merge操作。
Key:自然就是这个extent的可以。
Value部分包括<extent_head> + <data：32KByte>
这里要说明下extent_head的结构：
```
struct extent_head {
   int8_t flags;
   int8_t pad0;
   union{ 
         int16_t data_bmp; //extent当中有效的数据部分。bmp为0的部分在extent数据中并没有被存储
         int16_t merge_off;  //一次写入操作在extent内部的offset
  } ;
  char pad1[8];
}; //total 16 Byte
```

其中data_bmp共16 bit，这16个bit分别对应16个大小为4K的LBA哪些是有效的。无效的LBA将不会被存储，被认为是全0。
这里可以认为是一种压缩策略，比如一个exent中只有前4096Byte被写过，那么这个extent的value就不需要是64KB, 而是只需要存储4KB，并设置data_bmp为0x0001

flags表示： 
 - 0x01 表示当前extent是一个base数据，数据的有效区域由data_bmp决定。
 - 0x00 表示当前extent是一个merge的delta数据，数据的有效区域是从merge_off开始，长度为value长度(rocksdb提供）

在执行merge操作的时候，根据flags执行：

base + delta => new base

delta + delta => merged delta


如果是extent[0], 只有base数据包含了inode， delta里面不包含。inode不参与merge, 总是保留在结果里面

inode的大小为256Byte，使用和ext4几乎兼容的格式。

如何知道文件的大小？
记录在inode 里，每次文件长度变化就更新之，类似ext4, 数据和元数据分开更新


### 二，目录的实现
目录使用目录文件，也和ext4类似。这里只需要使用线性的目录文件即可，因为这里目录文件的目的主要是未来ls操作，而不是文件查找操作。

__如何从文件名查到inode ?__

在一个单独的column family 里，建立 从file name -> inode no的映射

`file_name_key -> inode_no` 这个映射保存在单独的column family里。
`file_name_key := <parent_dir_inode_no> +  '_' + <file_name_without_path>`
也就是说，打开一个文件时需要一层一层找到这个文件所在的目录的inode_no, 然后再和文件名拼接成一个key， 去查询最终的文件inode_no.

不直接使用文件的全路径作为Key, 是为了避免目录重命名时需要修改大量的K-V对。比如对于下面的目录结构：
```
/A  （inode 101)
 +-B  （inode 102)
   +-- 1.txt  （inode 103)
   +-- 2.txt  （inode 104)
```
z这里有两个文件，全路径分别为/A/B/1.txt和/A/B/2.txt。如果使用全路径作为作为key，那么存储的K-V对就是
```
/A/B/1.txt     ->   103
/A/B/2.txt     ->   104
```

如果有一天我们想重命名目录B为C， 那么这两个K-V对都要进行删除和重新插入。所以按我们的设计，这里这样记录：
```
102_1.txt    -> 103
102_2.txt    -> 104
```
这样即使目录重命名，但是他的inode no是不变的。这两个K-V对都不需要更改。需要更改的只是目录自己的映射，这里就是
`101_B   -> 102` 要被删除并重新创建为    `101_C  -> 102`

是否可以不使用目录文件? 
目录文件的目的是为了ls目录下面的文件。如果不使用目录文件，也可以借助类似rocksdb的prefix search这样的功能，比如要ls /A/B 这个目录，我们只需要`prefix search '102_'` 开头的key就可以了。遗憾的是LSM tree对prefix search是非常低效的，文件系统的规模变大以后，很可能导致所有Level的所有文件都被频繁全量访问。

### 三，Merge 操作
如果一次io 操作跨越了两个extent, 要分成两笔io 进行。后面讨论Merge 都只限定在单个k-v内部

每次io会带上offset, length 两个信息，作为冗余信息，offset使用相对于extent内部的offset。

无论是Base extent，还是delta extent，都带有exent_head, flags字段的bit0 如果是0就表示该extent是base extent, 1表示是delta extent. 如果是base extent, head中的data_bmp表示有效数据。对于delta extent, merge_off表示数据在extent中的偏移量。数据的长度由rocksdb 的value自身提供。