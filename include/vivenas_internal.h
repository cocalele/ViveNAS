#ifndef vivenas_internal_h__
#define vivenas_internal_h__

struct ViveSuperBlock;

#define VIVEFS_MAGIC_STR "vivefs_0"
#define VIVEFS_VER 0x00010000

#define LBA_SIZE 4096
#define LBA_SIZE_ORDER 12
struct pfs_extent_key {
	union {
		struct {
			__le64 extent_index;
			__le64 inode_no;
		};
		char keybuf[16];
	};
};

#define PFS_FULL_EXTENT_BMP  (uint16_t)0xffff
struct pfs_extent_head {
	int8_t flags;
	int8_t pad0;
	union {
		int16_t data_bmp; //extent当中有效的数据部分。bmp为0的部分在extent数据中并没有被存储
		int16_t merge_off;  //一次写入操作在extent内部的offset
	};
	char pad1[8];
}; //total 16 Byte

#define PFS_EXTENT_HEAD_SIZE sizeof(struct pfs_extent_head)

/**
 * special inode no, as defined in ext:
 * 0	No such inode, numberings starts at 1
 * 1	Defective block list
 * 2	Root directory
 * 3	User quotas
 * 4	Group quotas
 * 5	Boot loader
 * 6	Undelete directory
 * 7	Reserved group descriptors (for resizing filesystem)
 * 8	Journal
 * 9	Exclude inode (for snapshots)
 * 10	Replica inode
 * 11	First non-reserved inode (often lost + found)
 */ 
#define VN_ROOT_INO 2
#define VN_FIRST_USER_INO 12

static __always_inline int64_t deserialize_int64(const char* s) {
	return *(int64_t*)s;
}
void deserialize_superblock(const char* buf, ViveSuperBlock& sb);
std::string serialize_superblock(const ViveSuperBlock& sb);

#endif // vivenas_internal_h__