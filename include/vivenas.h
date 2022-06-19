#ifndef vivenas_h__
#define vivenas_h__

#include "rocksdb/db.h"
#include <rocksdb/utilities/transaction_db.h>
#include <memory>
#include <string>
#include <stdint.h>
#include <linux/types.h>

#define VIVE_INODE_SIZE 256
#define INODE_SEED_KEY "__inode_seed__"


#define FILE_MODE_FIFO 0x1000
#define FILE_MODE_CHAR_DEV 0x2000
#define FILE_MODE_DIR 0x4000
#define FILE_MODE_BLK_DEV 0x6000
#define FILE_MODE_REG 0x8000
#define FILE_MODE_SYM_LINK 0xA000
#define FILE_MODE_SOCKET 0xC000

#define IS_DIR(mode) ((mode & 0xf000) == FILE_MODE_DIR) 


struct ViveSuperBlock
{
	std::string magic;
	int32_t version;
};
/**
 * members of ViveInode have same means as that in ext4_inode
 */
struct ViveInode {
	__le16	i_mode;		/* File mode */ 
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
	explicit vn_inode_no_t(int64_t i):i_no(i){}
	__always_inline int64_t  to_int() {
		return i_no;
	}
	struct vn_inode_no_t from_int(int64_t i_no) {
		return vn_inode_no_t {i_no};
	}
};

struct ViveFile {
	ViveInode* inode;
	__le64  i_no;
	std::string file_name;
	__le64 parent_inode_no;
};

struct ViveFsContext {
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
//typedef uint64_t __le64;
//typedef uint16_t __le16;
//typedef uint32_t __le32;

struct vn_inode_iterator;


#define CHECKED_CALL(exp) do{ \
	Status s = exp;         \
	if (!s.ok()) {          \
	S5LOG_FATAL("Failed: `%s` status:%s", #exp, s.ToString().c_str()); \
	}                       \
}while(0)
vn_inode_no_t vn_lookup_inode_no(ViveFsContext* ctx, int64_t parent_inode_no, const char* file_name, /*out*/ ViveInode* inode);
vn_inode_no_t vn_create_file(ViveFsContext* ctx, int64_t parent_inode_no, const char* file_name, int16_t mode, /*out*/ ViveInode* inode_out);
struct ViveFile* vn_open_file(ViveFsContext* ctx, int64_t parent_inode_no, const char* file_name, int32_t flags, int16_t mode);
ssize_t vn_write(ViveFsContext* ctx, struct ViveFile* file, const char* in_buf, size_t len, off_t offset);
ssize_t vn_writev(ViveFsContext* ctx, struct ViveFile* file, struct iovec in_iov[], int iov_cnt, off_t offset);
ssize_t vn_read(ViveFsContext* ctx, struct ViveFile* file, char* out_buf, size_t len, off_t offset);
ssize_t vn_readv(ViveFsContext* ctx, struct ViveFile* file, struct iovec out_iov[] , int iov_cnt, off_t offset);
struct vn_inode_iterator* vn_begin_iterate_dir(ViveFsContext* ctx, int64_t parent_inode_no);
struct ViveInode* vn_next_inode(ViveFsContext* ctx, struct vn_inode_iterator* it, std::string* entry_name);
void vn_release_iterator(ViveFsContext* ctx, struct vn_inode_iterator* it);
int vn_fsync(ViveFsContext* ctx, struct ViveFile* file);
int vn_close_file(ViveFsContext* ctx, struct ViveFile* file);
int vn_delete(ViveFsContext* ctx, struct ViveFile* file);
int vn_rename_file(ViveFsContext* ctx, vn_inode_no_t old_dir, const char* old_name, vn_inode_no_t new_dir, const char* new_name);
#endif // vivenas_h__