#ifndef vivenas_internal_h__
#define vivenas_internal_h__
#define LBA_SIZE 4096
#define LBA_SIZE_ORDER 12
struct pfs_extent_key {
	union {
		struct {
			__le64 extent_index;
			__le64 inode_no;
		};
		char keybuf[16];
	}
};

#define PFS_FULL_EXTENT_BMP = 0xffff
struct pfs_extent_head {
	int8_t flags;
	int8_t pad0;
	union {
		int16_t data_bmp; //extent当中有效的数据部分。bmp为0的部分在extent数据中并没有被存储
		int16_t merge_off;  //一次写入操作在extent内部的offset
	};
	char pad1[8];
}; //total 16 Byte

#define EXT_HEAD_SIZE sizeof(struct pfs_extent_head)

#endif // vivenas_internal_h__