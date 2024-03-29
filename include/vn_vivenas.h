#ifndef vivenas_h__
#define vivenas_h__

#include <stdint.h>
#include <stddef.h>
#include <linux/types.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VIVEFS_INODE_SIZE 256
#define INODE_SEED_KEY "__inode_seed__"

typedef int64_t off_t;
//typedef uint64_t __le64;
//typedef uint16_t __le16;
//typedef uint32_t __le32;

struct vn_inode_iterator;
struct ViveInode;
struct ViveFile;
struct ViveFsContext;

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
 * https://www.gnu.org/software/coreutils/manual/html_node/File-timestamps.html
 * Standard POSIX files have three timestamps: the access timestamp (atime) of the last read,
 * the modification timestamp (mtime) of the last write, and the status change timestamp (ctime)
 * of the last change to the file��s meta-information. Some file systems support a fourth time:
 * the birth timestamp (birthtime) of when the file was created; by definition, birthtime never changes.
 * One common example of a ctime change is when the permissions of a file change. 
 * For efficiency reasons, many systems are lazy about updating atimes: when a program accesses a file,
 * they may delay updating the file��s atime, or may not update the file��s atime if the file has been
 * accessed recently, or may not update the atime at all. Similar laziness, though typically not quite
 * so extreme, applies to mtimes and ctimes.
 */
struct ViveInode {
	__le64  i_no; //inode number of this inode
	__le64	i_size;	/* Size in bytes */
	__le64	i_atime;	/* Access time */
	__le64	i_ctime;	/* Inode Change time */
	__le64	i_mtime;	/* Modification time */
	__le64	i_dtime;	/* Deletion Time */
	__le16	i_mode;		/* File mode */ //use file type like __S_IFDIR
	__le16	i_uid;		/* Low 16 bits of Owner Uid */
	__le16	i_gid;		/* Low 16 bits of Group Id */
	__le16	i_links_count;	/* Links count */
	__le32	i_flags;	/* File flags */
	__le32  i_extent_size;

	__le64  _reserve[23];
	__le32  _reserve_1;
	__le32  ref_cnt;
};

static __always_inline void vn_add_inode_ref(struct ViveInode* n)
{
	++n->ref_cnt;
}
static __always_inline void vn_dec_inode_ref(struct ViveInode* n)
{
	if (--n->ref_cnt == 0) {
		free(n);
	}
}
typedef int64_t inode_no_t;

inode_no_t vn_lookup_inode_no(struct ViveFsContext* ctx, inode_no_t parent_inode_no, const char* file_name, /*out*/struct  ViveInode** inode);
inode_no_t vn_create_file(struct ViveFsContext* ctx, inode_no_t parent_inode_no, const char* file_name, 
	                      int16_t mode, int16_t uid, int16_t gid, /*out*/ struct ViveInode** inode_out);
struct ViveFile* vn_open_file(struct ViveFsContext* ctx, inode_no_t parent_inode_no, const char* file_name, int32_t flags, int16_t mode);
struct ViveFile* vn_open_file_by_inode(struct ViveFsContext* ctx, struct ViveInode* inode, int32_t flags, int16_t mode);
size_t vn_write(struct ViveFsContext* ctx, struct ViveFile* file, const char* in_buf, size_t len, off_t offset);
size_t vn_writev(struct ViveFsContext* ctx, struct ViveFile* file, struct iovec in_iov[], int iov_cnt, off_t offset);
size_t vn_read(struct ViveFsContext* ctx, struct ViveFile* file, char* out_buf, size_t len, off_t offset);
size_t vn_readv(struct ViveFsContext* ctx, struct ViveFile* file, struct iovec out_iov[] , int iov_cnt, off_t offset);
struct vn_inode_iterator* vn_begin_iterate_dir(struct ViveFsContext* ctx, int64_t parent_inode_no);
struct ViveInode* vn_next_inode(struct ViveFsContext* ctx, struct vn_inode_iterator* it, char* entry_name, size_t buf_len);
int /*as bool*/ vn_iterator_has_next(struct vn_inode_iterator* it);
void vn_release_iterator(struct ViveFsContext* ctx, struct vn_inode_iterator* it);
int vn_fsync(struct ViveFsContext* ctx, struct ViveFile* file);
int vn_close_file(struct ViveFsContext* ctx, struct ViveFile* file);
int vn_unlink(struct ViveFsContext* ctx, int64_t parent_ino, const char* fname);
int vn_rename_file(struct ViveFsContext* ctx, inode_no_t old_dir_ino, const char* old_name, inode_no_t new_dir_ino, const char* new_name);
struct ViveFsContext* vn_mount(const char* db_path);
int vn_umount(struct ViveFsContext* ctx);
void vn_free_inode(struct ViveInode* inode);
inode_no_t vn_ino_of_file(struct ViveFile* f);
struct ViveInode* vn_get_root_inode(struct ViveFsContext* ctx);
int vn_flush_fs(struct ViveFsContext* ctx);
int vn_persist_inode(struct ViveFsContext* ctx,  struct ViveInode* inode);
void 	__PfAof_init();

#ifdef __cplusplus
}
#endif
#endif // vivenas_h__