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

typedef int64_t inode_no_t;

inode_no_t vn_lookup_inode_no(struct ViveFsContext* ctx, inode_no_t parent_inode_no, const char* file_name, /*out*/struct  ViveInode* inode);
inode_no_t vn_create_file(struct ViveFsContext* ctx, inode_no_t parent_inode_no, const char* file_name, int16_t mode, /*out*/ struct ViveInode** inode_out);
struct ViveFile* vn_open_file(struct ViveFsContext* ctx, inode_no_t parent_inode_no, const char* file_name, int32_t flags, int16_t mode);
struct ViveFile* vn_open_file_by_inode(struct ViveFsContext* ctx, inode_no_t ino, int32_t flags, int16_t mode);
size_t vn_write(struct ViveFsContext* ctx, struct ViveFile* file, const char* in_buf, size_t len, off_t offset);
size_t vn_writev(struct ViveFsContext* ctx, struct ViveFile* file, struct iovec in_iov[], int iov_cnt, off_t offset);
size_t vn_read(struct ViveFsContext* ctx, struct ViveFile* file, char* out_buf, size_t len, off_t offset);
size_t vn_readv(struct ViveFsContext* ctx, struct ViveFile* file, struct iovec out_iov[] , int iov_cnt, off_t offset);
struct vn_inode_iterator* vn_begin_iterate_dir(struct ViveFsContext* ctx, inode_no_t parent_inode_no);
struct ViveInode* vn_next_inode(struct ViveFsContext* ctx, struct vn_inode_iterator* it, const char* entry_name);
void vn_release_iterator(struct ViveFsContext* ctx, struct vn_inode_iterator* it);
int vn_fsync(struct ViveFsContext* ctx, struct ViveFile* file);
int vn_close_file(struct ViveFsContext* ctx, struct ViveFile* file);
int vn_delete(struct ViveFsContext* ctx, struct ViveFile* file);
int vn_rename_file(struct ViveFsContext* ctx, inode_no_t old_dir_ino, const char* old_name, inode_no_t new_dir_ino, const char* new_name);
struct ViveFsContext* vn_mount(const char* db_path);
void vn_free_inode(struct ViveInode* inode);
inode_no_t vn_ino_of_file(struct ViveFile* f);

void vn_say_hello(const char* s);//a function for debug

void 	__PfAof_init();

#ifdef __cplusplus
}
#endif
#endif // vivenas_h__