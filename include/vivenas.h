#ifndef vivenas_h__
#define vivenas_h__

#include "rocksdb/db.h"
#include <rocksdb/utilities/transaction_db.h>
#define VIVE_INODE_SIZE 256
#define INODE_SEED_KEY "__inode_seed__"

struct ViveSuperBlock
{
	string magic;
	int32_t version;
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

	int64_t inode_seed;
	int64_t generate_inode_no();
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

struct ViveFile {
	ViveInode* inode;
	__le64  i_no;
	std::string file_name;
	__le64 parent_inode_no;
};

#define CHECKED_CALL(exp) do{ \
	Status s = exp;         \
	if (!s.ok()) {          \
	S5LOG_FATAL("Failed: `%s` status:%s", ##exp, s.ToString().c_str()); \
	}                       \
}while(0)

#endif // vivenas_h__