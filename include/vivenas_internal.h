#ifndef vivenas_internal_h__
#define vivenas_internal_h__
#include "rocksdb/db.h"
#include <rocksdb/utilities/transaction_db.h>
#include <memory>
#include <string>
#include <stdint.h>

struct ViveSuperBlock;

#define VIVEFS_MAGIC_STR "vivefs_0"
#define VIVEFS_VER 0x00010000

#define LBA_SIZE 4096
#define LBA_SIZE_ORDER 12
#define VIVEFS_EXTENT_SIZE (64<<10)

struct ViveSuperBlock
{
	std::string magic;
	int32_t version;
};



/* File types.   defined in stat.h */
//#define	__S_IFDIR	0040000	/* Directory.  */
//#define	__S_IFCHR	0020000	/* Character device.  */
//#define	__S_IFBLK	0060000	/* Block device.  */
//#define	__S_IFREG	0100000	/* Regular file.  */
//#define	__S_IFIFO	0010000	/* FIFO.  */
//#define	__S_IFLNK	0120000	/* Symbolic link.  */
//#define	__S_IFSOCK	0140000	/* Socket.  */

/* and following macro defined fcntl.h*/
//# define S_IFMT		__S_IFMT
//# define S_IFDIR	__S_IFDIR
//# define S_IFCHR	__S_IFCHR
//# define S_IFBLK	__S_IFBLK
//# define S_IFREG	__S_IFREG

// S_ISREG ... can used to judge file type


/**
 * members of ViveInode have same means as that in ext4_inode
 */
struct ViveInode {
	__le16	i_mode;		/* File mode */ //use file type like __S_IFDIR
	__le16	i_uid;		/* Low 16 bits of Owner Uid */
	__le64	i_size;	/* Size in bytes */
	__le64	i_atime;	/* Access time */
	__le64	i_ctime;	/* Inode Change time */
	__le64	i_mtime;	/* Modification time */
	__le64	i_dtime;	/* Deletion Time */
	__le16	i_gid;		/* Low 16 bits of Group Id */
	__le16	i_links_count;	/* Links count */
	__le32	i_flags;	/* File flags */
	__le64  i_no; //inode number of this inode
	__le32  i_extent_size;

};
struct vn_inode_no_t {
	int64_t i_no;
	explicit vn_inode_no_t(int64_t i) :i_no(i) {}
	__always_inline int64_t  to_int() {
		return i_no;
	}
	struct vn_inode_no_t from_int(int64_t i_no) {
		return vn_inode_no_t{ i_no };
	}
};

struct ViveFile {
	ViveInode* inode;
	__le64  i_no;
	std::string file_name;
	__le64 parent_inode_no;
};

struct ViveFsContext {
	ViveFsContext();
	~ViveFsContext();
	std::string db_path;
	ROCKSDB_NAMESPACE::TransactionDB* db;
	ROCKSDB_NAMESPACE::ColumnFamilyHandle* default_cf;
	ROCKSDB_NAMESPACE::ColumnFamilyHandle* meta_cf;
	ROCKSDB_NAMESPACE::ColumnFamilyHandle* data_cf;

	ROCKSDB_NAMESPACE::WriteOptions meta_opt;
	ROCKSDB_NAMESPACE::WriteOptions data_opt;
	ROCKSDB_NAMESPACE::ReadOptions read_opt;

	ViveInode root_inode;
	std::unique_ptr<ViveFile> root_file;

	int64_t inode_seed;
	int64_t generate_inode_no();
};
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
#define CHECKED_CALL(exp) do{ \
	Status s = exp;         \
	if (!s.ok()) {          \
	S5LOG_FATAL("Failed: `%s` status:%s", #exp, s.ToString().c_str()); \
	}                       \
}while(0)
static __always_inline int64_t deserialize_int64(const char* s) {
	return *(int64_t*)s;
}
void deserialize_superblock(const char* buf, ViveSuperBlock& sb);
std::string serialize_superblock(const ViveSuperBlock& sb);

#endif // vivenas_internal_h__