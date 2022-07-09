

#include <unistd.h>
#include <stdlib.h>

#include "vn_fsal.h"
#include "vivenas.h"
#include <string>
#include <pf_utils.h>
extern "C"{
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_localfs.h"
#include "FSAL/fsal_commonlib.h"
#include "city.h"
#include "nfs_file_handle.h"
#include "display.h"
#ifdef USE_LTTNG
#include "gsh_lttng/fsal_mem.h"
#endif
#include "nfs_core.h"
#include "common_utils.h"
}

using namespace std;
static void vn_release(struct fsal_obj_handle* obj_hdl);

/* Atomic uint64_t that is used to generate inode numbers in the mem FS */
uint64_t vn_inode_number = 1;

/* helpers
 */

static inline int
vn_n_cmpf(const struct avltree_node* lhs,
	const struct avltree_node* rhs)
{
	struct vn_dirent* lk, * rk;

	lk = avltree_container_of(lhs, struct vn_dirent, avl_n);
	rk = avltree_container_of(rhs, struct vn_dirent, avl_n);

	return strcmp(lk->d_name, rk->d_name);
}

static inline int
vn_i_cmpf(const struct avltree_node* lhs,
	const struct avltree_node* rhs)
{
	struct vn_dirent* lk, * rk;

	lk = avltree_container_of(lhs, struct vn_dirent, avl_i);
	rk = avltree_container_of(rhs, struct vn_dirent, avl_i);

	if (lk->d_index < rk->d_index)
		return -1;

	if (lk->d_index == rk->d_index)
		return 0;

	return 1;
}

/**
 * @brief Clean up and free an object handle
 *
 * @param[in] obj_hdl	Handle to release
 */
static void vn_cleanup(struct vn_fsal_obj_handle* myself)
{
	struct vn_fsal_export* mfe;

	mfe = myself->mfo_exp;

	fsal_obj_handle_fini(&myself->obj_handle);

	LogDebug(COMPONENT_FSAL,
		"Releasing obj_hdl=%p, myself=%p, name=%s",
		&myself->obj_handle, myself, myself->name.c_str());

	switch (myself->obj_handle.type) {
	case DIRECTORY:
		/* Empty directory */
		//vn_clean_all_dirents(myself);
		break;
	case REGULAR_FILE:
		break;
	case SYMBOLIC_LINK:
		//gsh_free(myself->mh_symlink.link_contents);
		break;
	case SOCKET_FILE:
	case CHARACTER_FILE:
	case BLOCK_FILE:
	case FIFO_FILE:
		break;
	default:
		break;
	}

	PTHREAD_RWLOCK_wrlock(&mfe->mfe_exp_lock);
	vn_free_handle(myself);
	PTHREAD_RWLOCK_unlock(&mfe->mfe_exp_lock);
}

#define vn_int_get_ref(myself) _vn_int_get_ref(myself, __func__, __LINE__)
/**
 * @brief Get a ref for a handle
 *
 * @param[in] myself	Handle to ref
 * @param[in] func	Function getting ref
 * @param[in] line	Line getting ref
 */
static void _vn_int_get_ref(struct vn_fsal_obj_handle* myself,
	const char* func, int line)
{
#ifdef USE_LTTNG
	int32_t refcount =
#endif
		atomic_inc_int32_t(&myself->refcount);

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_get_ref, func, line, &myself->obj_handle,
		myself->m_name, refcount);
#endif
}

#define vn_int_put_ref(myself) _vn_int_put_ref(myself, __func__, __LINE__)
/**
 * @brief Put a ref for a handle
 *
 * If this is the last ref, clean up and free the handle
 *
 * @param[in] myself	Handle to ref
 * @param[in] func	Function getting ref
 * @param[in] line	Line getting ref
 */
static void _vn_int_put_ref(struct vn_fsal_obj_handle* myself,
	const char* func, int line)
{
	int32_t refcount = atomic_dec_int32_t(&myself->refcount);

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_put_ref, func, line, &myself->obj_handle,
		myself->m_name, refcount);
#endif

	if (refcount == 0) {
		vn_cleanup(myself);
	}
}

/**
 * @brief Construct the fs opaque part of a mem nfsv4 handle
 *
 * Given the components of a mem nfsv4 handle, the nfsv4 handle is
 * created by concatenating the components. This is the fs opaque piece
 * of struct file_handle_v4 and what is sent over the wire.
 *
 * @param[in] myself	Obj to create handle for
 *
 * @return The nfsv4 mem file handle as a char *
 */
static void package_vn_handle(struct vn_fsal_obj_handle* myself)
{
	char buf[MAXPATHLEN];
	uint16_t len_fileid, len_inode;
	uint64_t hashkey;
	int opaque_bytes_used = 0, pathlen = 0;

	memset(buf, 0, sizeof(buf));

	/* Make hashkey */
	len_fileid = sizeof(myself->obj_handle.fileid);
	memcpy(buf, &myself->obj_handle.fileid, len_fileid);
	len_inode = sizeof(myself->inode);
	memcpy(buf + len_fileid, &myself->inode,
		MIN(len_inode, sizeof(buf) - len_inode));
	hashkey = CityHash64(buf, sizeof(buf));

	memcpy(myself->handle, &hashkey, sizeof(hashkey));
	opaque_bytes_used += (int)sizeof(hashkey);

	/* include length of the name in the handle.
	 * MAXPATHLEN=4096 ... max path length can be contained in a short int.
	 */
	memcpy(myself->handle + opaque_bytes_used, &len_inode, sizeof(len_inode));
	opaque_bytes_used += (int)sizeof(len_inode);

	/* Either the nfsv4 fh opaque size or the length of the name.
	 * Ideally we can include entire mem name for guaranteed
	 * uniqueness of mem handles.
	 */
	pathlen = MIN(V4_FH_OPAQUE_SIZE - opaque_bytes_used, len_inode);
	memcpy(myself->handle + opaque_bytes_used, &myself->inode, pathlen);
	opaque_bytes_used += pathlen;

	/* If there is more space in the opaque handle due to a short mem
	 * path ... zero it.
	 */
	if (opaque_bytes_used < V4_FH_OPAQUE_SIZE) {
		memset(myself->handle + opaque_bytes_used, 0,
			V4_FH_OPAQUE_SIZE - opaque_bytes_used);
	}
}



/**
 * @brief Update the change attribute of the FSAL object
 *
 * @note Caller must hold the obj_lock on the obj
 *
 * @param[in] obj	FSAL obj which was modified
 */
static void vn_update_change_locked(struct vn_fsal_obj_handle* obj)
{
	now(&obj->attrs.mtime);
	obj->attrs.ctime = obj->attrs.mtime;
	obj->attrs.change = timespec_to_nsecs(&obj->attrs.mtime);
}


/**
 * @brief Recursively clean all objs/dirents on an export
 *
 * @note Caller MUST hold export lock for write
 *
 * @param[in] root	Root to clean
 * @return Return description
 */
void vn_clean_export(struct vn_fsal_obj_handle* root)
{

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_inuse, __func__, __LINE__, &root->obj_handle,
		root->attrs.numlinks, root->is_export);
#endif
	LogWarn(COMPONENT_FSAL, "vn_clean_export not implemented");
}


static void vn_copy_attrs_mask(struct fsal_attrlist* attrs_in,
	struct fsal_attrlist* attrs_out)
{
	/* Use full timer resolution */
	now(&attrs_out->ctime);

	if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_SIZE)) {
		attrs_out->filesize = attrs_in->filesize;
	}

	if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_MODE)) {
		attrs_out->mode = attrs_in->mode & (~S_IFMT & 0xFFFF) &
			~op_ctx->fsal_export->exp_ops.fs_umask(
				op_ctx->fsal_export);
	}

	if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_OWNER)) {
		attrs_out->owner = attrs_in->owner;
	}

	if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_GROUP)) {
		attrs_out->group = attrs_in->group;
	}

	if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTRS_SET_TIME)) {
		if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_ATIME_SERVER)) {
			attrs_out->atime.tv_sec = 0;
			attrs_out->atime.tv_nsec = UTIME_NOW;
		}
		else if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_ATIME)) {
			attrs_out->atime = attrs_in->atime;
		}
		else {
			attrs_out->atime = attrs_out->ctime;
		}

		if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_MTIME_SERVER)) {
			attrs_out->mtime.tv_sec = 0;
			attrs_out->mtime.tv_nsec = UTIME_NOW;
		}
		else if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_MTIME)) {
			attrs_out->mtime = attrs_in->mtime;
		}
		else {
			attrs_out->mtime = attrs_out->ctime;
		}
	}

	if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_CREATION)) {
		attrs_out->creation = attrs_in->creation;
	}

	if (FSAL_TEST_MASK(attrs_in->valid_mask, ATTR_SPACEUSED)) {
		attrs_out->spaceused = attrs_in->spaceused;
	}
	else {
		attrs_out->spaceused = attrs_out->filesize;
	}

	/* XXX TODO copy ACL */

	/** @todo FSF - this calculation may be different than what particular
	 *              FSALs use. Is that a problem?
	 */
	attrs_out->change = timespec_to_nsecs(&attrs_out->ctime);
}




/**
 * @brief Allocate a MEM handle
 *
 * @param[in] parent	Parent directory handle
 * @param[in] name	Name of handle to allocate
 * @param[in] type	Type of handle to allocate
 * @param[in] mfe	MEM Export owning new handle
 * @param[in] attrs	Attributes of new handle
 * @return Handle on success, NULL on failure
 */
static struct vn_fsal_obj_handle*
vn_alloc_handle(struct vn_fsal_obj_handle* parent,
			const char* name,
			ViveInode* inode,
			struct vn_fsal_export* mfe)
{
	struct vn_fsal_obj_handle* hdl;
	struct fsal_filesystem* fs = mfe->m_export.root_fs;
	struct fsal_export* exp_hdl = &mfe->m_export;

	hdl = (vn_fsal_obj_handle*)gsh_calloc(1, sizeof(struct vn_fsal_obj_handle));
	hdl->mfo_exp = mfe;

	package_vn_handle(hdl);
	hdl->name = name;
	hdl->obj_handle.type = posix2fsal_type(inode->i_mode);
	//hdl->dev = posix2fsal_devt(stat->st_dev);
	//hdl->obj_handle.up_ops = mfe->m_export.up_ops;
	hdl->obj_handle.fs = fs;
	LogDebug(COMPONENT_FSAL,
		"Creating object %p for file %s of type %s on filesystem %p ",
		hdl, name, object_file_type_to_str(hdl->obj_handle.type),
		fs);

	//hdl->vfile = NULL;


	fsal_obj_handle_init(&hdl->obj_handle, exp_hdl,
		posix2fsal_type(inode->i_mode));
	
	if (fs) {

		hdl->obj_handle.fsid = fs->fsid;
	}
	hdl->obj_handle.fileid = inode->i_no;
#ifdef VFS_NO_MDCACHE
	hdl->obj_handle.state_hdl = vfs_state_locate(&hdl->obj_handle);
#endif /* VFS_NO_MDCACHE */
	hdl->obj_handle.obj_ops = &ViveNASM.handle_ops;
	memcpy(&hdl->inode, inode, sizeof(hdl->inode));
	hdl->refcount = 1;
	S5LOG_DEBUG("alloc vn_fsal_obj_handle:%p", hdl);
	return hdl;
}


static fsal_status_t vn_create_obj(struct vn_fsal_obj_handle* parent,
	object_file_type_t type,
	const char* name,
	struct fsal_attrlist* attrs_in,
	struct fsal_obj_handle** new_obj,
	struct fsal_attrlist* attrs_out)
{
	struct vn_fsal_export* mfe = container_of(op_ctx->fsal_export,
		struct vn_fsal_export,
		m_export);
	struct vn_fsal_obj_handle* hdl;

	*new_obj = NULL;		/* poison it */

	if (parent->obj_handle.type != DIRECTORY) {
		LogCrit(COMPONENT_FSAL,
			"Parent handle is not a directory. hdl = 0x%p",
			parent);
		return fsalstat(ERR_FSAL_NOTDIR, 0);
	}

	vn_inode_no_t i_no =  vn_lookup_inode_no(mfe->mount_ctx, parent->inode.i_no, name, NULL);
	if (i_no.to_int() > 0) {
		/* It already exists */
		return fsalstat(ERR_FSAL_EXIST, 0);
	}


	ViveInode inode;
	i_no = vn_create_file(mfe->mount_ctx, parent->inode.i_no, name, attrs_in->mode, &inode);
	if (i_no.to_int() < 0) {
		LogCrit(COMPONENT_FSAL, "Failed create file:%s", name);
		return fsalstat(ERR_FSAL_IO, 0);
	}
	/* allocate an obj_handle and fill it up */
	hdl = vn_alloc_handle(parent, name, &inode,	mfe);
	if (!hdl)
		return fsalstat(ERR_FSAL_NOMEM, 0);
	if (attrs_out != NULL)
		fsal_copy_attrs(attrs_out, &hdl->attrs, false);

	*new_obj = &hdl->obj_handle;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* handle methods
 */

 /**
  * @brief Lookup a file
  *
  * @param[in] parent	Parent directory
  * @param[in] path	Path to lookup
  * @param[out] handle	Found handle, on success
  * @param[out] attrs_out	Attributes of found handle
  * @return FSAL status
  */
static fsal_status_t vn_lookup(struct fsal_obj_handle* parent,
	const char* path,
	struct fsal_obj_handle** handle,
	struct fsal_attrlist* attrs_out)
{
	struct vn_fsal_obj_handle* myself, * hdl = NULL;
	fsal_status_t status;
	S5LOG_DEBUG("vn_lookup on %s", path);
	myself = container_of(parent,
		struct vn_fsal_obj_handle,
		obj_handle);

	/* Check if this context already holds the lock on
	 * this directory.
	 */
	if (op_ctx->fsal_private != parent)
		PTHREAD_RWLOCK_rdlock(&parent->obj_lock);
	
	ViveInode inode;
	vn_inode_no_t ino = vn_lookup_inode_no(myself->mfo_exp->mount_ctx, myself->inode.i_no, path, &inode);
	if (ino.to_int() < 0) {
		status = fsalstat(ERR_FSAL_NOENT, 0);
		goto out;
	}
	hdl = vn_alloc_handle(myself, path, &inode, myself->mfo_exp);
	if (!hdl) {
		status = fsalstat(ERR_FSAL_NOMEM, 0);
		goto out;
	}
	
	*handle = &hdl->obj_handle;

out:
	if (op_ctx->fsal_private != parent)
		PTHREAD_RWLOCK_unlock(&parent->obj_lock);

	if (!FSAL_IS_ERROR(status) && attrs_out != NULL) {
		/* This is unlocked, however, for the most part, attributes
		 * are read-only. Come back later and do some lock protection.
		 */
		fsal_copy_attrs(attrs_out, &hdl->attrs, false);
	}

	return status;
}

static void set_attr_from_inode(struct fsal_attrlist *attrs, ViveInode* inode)
{
	attrs->fileid = inode->i_no;
	//attrs->atime = inode->i_atime;
	//attrs->ctime = inode->i_ctime;
	attrs->filesize = inode->i_size;
	attrs->mode = inode->i_mode;
	attrs->type = posix2fsal_type(inode->i_mode);
}

/**
 * @brief Read a directory
 *
 * @param[in] dir_hdl	the directory to read
 * @param[in] whence	where to start (next)
 * @param[in] dir_state	pass thru of state to callback
 * @param[in] cb	callback function
 * @param[out] eof	eof marker true == end of dir
 */

static fsal_status_t vn_readdir(struct fsal_obj_handle* dir_hdl,
	fsal_cookie_t* whence,
	void* dir_state,
	fsal_readdir_cb cb,
	attrmask_t attrmask,
	bool* eof)
{
	struct vn_fsal_obj_handle* myself;
	fsal_cookie_t cookie = 0;
	struct fsal_attrlist attrs;
	enum fsal_dir_result cb_rc;
	int count = 0;
	myself = container_of(dir_hdl,
		struct vn_fsal_obj_handle,
		obj_handle);

	if (whence != NULL)
		cookie = *whence;

	*eof = true;

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_readdir, __func__, __LINE__, dir_hdl,
		myself->m_name, cookie);
#endif
	S5LOG_DEBUG("readdir hdl = % p, name = % s",
		myself, myself->name.c_str());

	PTHREAD_RWLOCK_rdlock(&dir_hdl->obj_lock);

	/* Use fsal_private to signal to lookup that we hold
	 * the lock.
	 */
	op_ctx->fsal_private = dir_hdl;

	struct vn_inode_iterator* it;
	if(whence == NULL){
		it = vn_begin_iterate_dir(myself->mfo_exp->mount_ctx, myself->inode.i_no);
		S5LOG_DEBUG("Create dir iterator:%p", it);
	}
	else {
		it = (struct vn_inode_iterator*)whence;
		S5LOG_DEBUG("reuse dir iterator:%p", it);

	}
	
	string entry_name;
	ViveInode* inode;
	/* Always run in index order */
	for (inode = vn_next_inode(myself->mfo_exp->mount_ctx, it, &entry_name); inode != NULL;) {
		fsal_prepare_attrs(&attrs, attrmask);
		set_attr_from_inode(&attrs, inode);

		S5LOG_DEBUG("readdir return:%s", entry_name.c_str());
		struct vn_fsal_obj_handle *hdl = vn_alloc_handle(myself, entry_name.c_str(), inode, myself->mfo_exp);
				
		cb_rc = cb(entry_name.c_str(), &hdl->obj_handle, &attrs,
			dir_state, cookie);

		fsal_release_attrs(&attrs);

		count++;
		
		if (cb_rc >= DIR_TERMINATE) {
			*eof = false;
			vn_release_iterator(myself->mfo_exp->mount_ctx, it);
			break;
		}
	}

	if(*eof == true) {
		S5LOG_DEBUG("release dir iterator:%p", it);

		vn_release_iterator(myself->mfo_exp->mount_ctx, it);
	}
	op_ctx->fsal_private = NULL;

	PTHREAD_RWLOCK_unlock(&dir_hdl->obj_lock);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * While FSAL_MEM is a support_ex FSAL, it doesn't actually support
 * setting attributes, so only the mode attribute is relevant. Any other
 * attributes set on creation will be ignored. The owner and group will be
 * set from the active credentials.
 *
 * @param[in]     dir_hdl   Directory in which to create the directory
 * @param[in]     name      Name of directory to create
 * @param[in]     attrs_in  Attributes to set on newly created object
 * @param[out]    new_obj   Newly created object
 * @param[in,out] attrs_out Optional attributes for newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */
static fsal_status_t vn_mkdir(struct fsal_obj_handle* dir_hdl,
	const char* name,
	struct fsal_attrlist* attrs_in,
	struct fsal_obj_handle** new_obj,
	struct fsal_attrlist* attrs_out)
{
	struct vn_fsal_obj_handle* parent =
		container_of(dir_hdl, struct vn_fsal_obj_handle, obj_handle);

	LogDebug(COMPONENT_FSAL, "mkdir %s", name);

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_mkdir, __func__, __LINE__, dir_hdl,
		parent->m_name, name);
#endif

	return vn_create_obj(parent, DIRECTORY, name, attrs_in, new_obj,
		attrs_out);
}

/**
 * @brief Make a device node
 *
 * @param[in] dir_hdl	Parent directory handle
 * @param[in] name	Name of new node
 * @param[in] nodetype	Type of new node
 * @param[in] attrs_in	Attributes for new node
 * @param[out] new_obj	New object handle on success
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t vn_mknode(struct fsal_obj_handle* dir_hdl,
	const char* name, object_file_type_t nodetype,
	struct fsal_attrlist* attrs_in,
	struct fsal_obj_handle** new_obj,
	struct fsal_attrlist* attrs_out)
{
	struct vn_fsal_obj_handle* hdl, * parent =
		container_of(dir_hdl, struct vn_fsal_obj_handle, obj_handle);
	fsal_status_t status;

	LogDebug(COMPONENT_FSAL, "mknode %s", name);

	status = vn_create_obj(parent, nodetype, name, attrs_in, new_obj,
		attrs_out);
	if (unlikely(FSAL_IS_ERROR(status)))
		return status;

	hdl = container_of(*new_obj, struct vn_fsal_obj_handle, obj_handle);


	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Make a symlink
 *
 * @param[in] dir_hdl	Parent directory handle
 * @param[in] name	Name of new node
 * @param[in] link_path	Contents of symlink
 * @param[in] attrs_in	Attributes for new simlink
 * @param[out] new_obj	New object handle on success
 *
 * @note This returns an INITIAL ref'd entry on success
 * @return FSAL status
 */
static fsal_status_t vn_symlink(struct fsal_obj_handle* dir_hdl,
	const char* name, const char* link_path,
	struct fsal_attrlist* attrs_in,
	struct fsal_obj_handle** new_obj,
	struct fsal_attrlist* attrs_out)
{
	struct vn_fsal_obj_handle* hdl, * parent =
		container_of(dir_hdl, struct vn_fsal_obj_handle, obj_handle);
	fsal_status_t status;

	LogDebug(COMPONENT_FSAL, "symlink %s", name);

	status = vn_create_obj(parent, SYMBOLIC_LINK, name, attrs_in, new_obj,
		attrs_out);
	if (unlikely(FSAL_IS_ERROR(status)))
		return status;

	hdl = container_of(*new_obj, struct vn_fsal_obj_handle, obj_handle);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Read a symlink
 *
 * @param[in] obj_hdl	Handle for symlink
 * @param[out] link_content	Buffer to fill with link contents
 * @param[in] refresh	If true, refresh attributes on symlink
 * @return FSAL status
 */
static fsal_status_t vn_readlink(struct fsal_obj_handle* obj_hdl,
	struct gsh_buffdesc* link_content,
	bool refresh)
{
	struct vn_fsal_obj_handle* myself =
		container_of(obj_hdl, struct vn_fsal_obj_handle, obj_handle);
	LogCrit(COMPONENT_FSAL,
		"Handle is not a symlink. hdl = 0x%p",
		obj_hdl);

	if (obj_hdl->type != SYMBOLIC_LINK) {
		LogCrit(COMPONENT_FSAL,
			"Handle is not a symlink. hdl = 0x%p",
			obj_hdl);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	//link_content->len = strlen(myself->mh_symlink.link_contents) + 1;
	//link_content->addr = gsh_strdup(myself->mh_symlink.link_contents);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Get attributes for a file
 *
 * @param[in] obj_hdl	File to get
 * @param[out] outattrs	Attributes for file
 * @return FSAL status
 */
static fsal_status_t vn_getattrs(struct fsal_obj_handle* obj_hdl,
	struct fsal_attrlist* outattrs)
{
	struct vn_fsal_obj_handle* myself =
		container_of(obj_hdl, struct vn_fsal_obj_handle, obj_handle);

	myself->attrs.numlinks = myself->inode.i_links_count;

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_getattrs, __func__, __LINE__, obj_hdl,
		myself->m_name, myself->attrs.filesize,
		myself->attrs.numlinks, myself->attrs.change);
#endif
	S5LOG_DEBUG(
		"vn_getattrs hdl=%p, name=%s numlinks %" PRIu32,
		myself,
		myself->name.c_str(),
		myself->attrs.numlinks);
	struct stat fstat;
	
	fstat.st_atime = myself->inode.i_atime;
	fstat.st_ctime = myself->inode.i_ctime;
	fstat.st_blksize = 4096;
	fstat.st_dev = 0;
	fstat.st_ino = myself->inode.i_no;
	fstat.st_nlink = myself->inode.i_links_count;
	fstat.st_gid = myself->inode.i_gid;
	fstat.st_uid = myself->inode.i_uid;
	fstat.st_mode = myself->inode.i_mode;
	fstat.st_size = myself->inode.i_size;
	fstat.st_blocks = (fstat.st_size + fstat.st_blksize  -1)/ fstat.st_blksize;

	posix2fsal_attributes_all(&fstat, &myself->attrs);
	fsal_copy_attrs(outattrs, &myself->attrs, false);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Set attributes on an object
 *
 * This function sets attributes on an object.  Which attributes are
 * set is determined by attrs_set->valid_mask. The FSAL must manage bypass
 * or not of share reservations, and a state may be passed.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 * @param[in] attrs_set Attributes to set
 *
 * @return FSAL status.
 */
fsal_status_t vn_setattr2(struct fsal_obj_handle* obj_hdl,
	bool bypass,
	struct state_t* state,
	struct fsal_attrlist* attrs_set)
{
	struct vn_fsal_obj_handle* myself =
		container_of(obj_hdl, struct vn_fsal_obj_handle, obj_handle);

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrs_set->valid_mask, ATTR_MODE))
		attrs_set->mode &=
		~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	/* Test if size is being set, make sure file is regular and if so,
	 * require a read/write file descriptor.
	 */
	if (FSAL_TEST_MASK(attrs_set->valid_mask, ATTR_SIZE) &&
		obj_hdl->type != REGULAR_FILE) {
		LogFullDebug(COMPONENT_FSAL,
			"Setting size on non-regular file");
		return fsalstat(ERR_FSAL_INVAL, EINVAL);
	}

	vn_copy_attrs_mask(attrs_set, &myself->attrs);

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_setattrs, __func__, __LINE__, obj_hdl,
		myself->m_name, myself->attrs.filesize,
		myself->attrs.numlinks, myself->attrs.change);
#endif
	return fsalstat(ERR_FSAL_NO_ERROR, EINVAL);
}

/**
 * @brief Hard link an obj
 *
 * @param[in] obj_hdl	File to link
 * @param[in] dir_hdl	Directory to link into
 * @param[in] name	Name to use for link
 *
 * @return FSAL status.
 */
fsal_status_t vn_link(struct fsal_obj_handle* obj_hdl,
	struct fsal_obj_handle* dir_hdl,
	const char* name)
{
	struct vn_fsal_obj_handle* myself =
		container_of(obj_hdl, struct vn_fsal_obj_handle, obj_handle);
	struct vn_fsal_obj_handle* dir =
		container_of(dir_hdl, struct vn_fsal_obj_handle, obj_handle);
	struct vn_fsal_obj_handle* hdl;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };

	vn_inode_no_t ino = vn_lookup_inode_no(myself->mfo_exp->mount_ctx, dir->inode.i_no, name, NULL);
	if (ino.to_int()>0) {
		/* It already exists */
		return fsalstat(ERR_FSAL_EXIST, 0);
	}
	LogCrit(COMPONENT_FSAL, "link not implemented");


	myself->attrs.numlinks++;

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_link, __func__, __LINE__, dir_hdl, dir->m_name,
		obj_hdl, myself->m_name, name, myself->attrs.numlinks);
#endif

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Unlink a file
 *
 * @param[in] dir_hdl	Parent directory handle
 * @param[in] obj_hdl	Object being removed
 * @param[in] name	Name of object to remove
 * @return FSAL status
 */
static fsal_status_t vn_unlink(struct fsal_obj_handle* dir_hdl,
	struct fsal_obj_handle* obj_hdl,
	const char* name)
{
	struct vn_fsal_obj_handle* myself, *parent_dir;
	fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

	parent_dir = container_of(dir_hdl, struct vn_fsal_obj_handle, obj_handle);
	myself = container_of(obj_hdl, struct vn_fsal_obj_handle, obj_handle);
	assert(strcmp(name, myself->name.c_str()) == 0);

	
	if(__sync_sub_and_fetch(&myself->inode.i_links_count, 1) == 0) {
		LogInfo(COMPONENT_FSAL, "Delete file:%ld_%s", parent_dir->inode.i_no, name);
		vn_delete(myself->mfo_exp->mount_ctx,  myself->vfile);
	} else {
		S5LOG_ERROR("BUG: link count not persisted if not 0");
	}
	
	return fsalstat(fsal_error, 0);
}

/**
 * @brief Close a file's global descriptor
 *
 * @param[in] obj_hdl    File on which to operate
 *
 * @return FSAL status.
 */

fsal_status_t vn_close(struct fsal_obj_handle* obj_hdl)
{
	struct vn_fsal_obj_handle* myself = container_of(obj_hdl,
		struct vn_fsal_obj_handle, obj_handle);

	assert(obj_hdl->type == REGULAR_FILE);

	/* Take write lock on object to protect file descriptor.
	 * This can block over an I/O operation.
	 */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	vn_close_file(myself->mfo_exp->mount_ctx, myself->vfile);
	myself->vfile = NULL;

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);;
}

/**
 * @brief Rename an object
 *
 * Rename the given object from @a old_name in @a olddir_hdl to @a new_name in
 * @a newdir_hdl.  The old and new directories may be the same.
 *
 * @param[in] obj_hdl	Object to rename
 * @param[in] olddir_hdl	Directory containing @a obj_hdl
 * @param[in] old_name	Current name of @a obj_hdl
 * @param[in] newdir_hdl	Directory to move @a obj_hdl to
 * @param[in] new_name	Name to rename @a obj_hdl to
 * @return FSAL status
 */
static fsal_status_t vn_rename(struct fsal_obj_handle* obj_hdl,
	struct fsal_obj_handle* olddir_hdl,
	const char* old_name,
	struct fsal_obj_handle* newdir_hdl,
	const char* new_name)
{
	struct vn_fsal_obj_handle* vn_olddir =
		container_of(olddir_hdl, struct vn_fsal_obj_handle,
			obj_handle);
	struct vn_fsal_obj_handle* vn_newdir =
		container_of(newdir_hdl, struct vn_fsal_obj_handle,
			obj_handle);
	
	int rc = vn_rename_file(vn_olddir->mfo_exp->mount_ctx, 
		vn_inode_no_t(vn_olddir->inode.i_no), old_name, 
		vn_inode_no_t(vn_newdir->inode.i_no), new_name);

	
	return fsalstat(rc == 0 ? ERR_FSAL_NO_ERROR : ERR_FSAL_IO, rc);
}



/**
 * @brief Open a file descriptor for read or write and possibly create
 *
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attrs_in              Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] attrs_out         Optional attributes for newly created object
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */
fsal_status_t vn_open2(struct fsal_obj_handle* obj_hdl,
	struct state_t* state,
	fsal_openflags_t openflags,
	enum fsal_create_mode createmode,
	const char* name,
	struct fsal_attrlist* attrs_set,
	fsal_verifier_t verifier,
	struct fsal_obj_handle** new_obj,
	struct fsal_attrlist* attrs_out,
	bool* caller_perm_check)
{
	struct vn_fsal_obj_handle* myself = NULL;
	bool created = false;

	int posix_flags = 0;
	mode_t unix_mode = 0000;


	myself = container_of(obj_hdl, struct vn_fsal_obj_handle, obj_handle);

	fsal2posix_openflags(openflags, &posix_flags);
	unix_mode = myself->inode.i_mode &
		~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	//If Name is NULL, obj_hdl is the file itself, otherwise obj_hdl is the parent directory.
	if (name == NULL) {
		if (myself->vfile != NULL)
			goto done;
	} else {
		ViveFile* f = vn_open_file(myself->mfo_exp->mount_ctx, myself->inode.i_no, name, posix_flags, unix_mode);
		if (f == NULL)
			return fsalstat(ERR_FSAL_NOENT, 0);
		struct vn_fsal_obj_handle* file_hdl = vn_alloc_handle(myself, name, f->inode, myself->mfo_exp);
		file_hdl->vfile = f;
		*new_obj = &file_hdl->obj_handle;

	}

done:
	created = (posix_flags & O_EXCL) != 0;
	*caller_perm_check = !created;
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}



/**
 * @brief Read data from a file
 *
 * This function reads data from the given file. The FSAL must be able to
 * perform the read whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations.  This is
 * an (optionally) asynchronous call.  When the I/O is complete, the done
 * callback is called with the results.
 *
 * @param[in]     obj_hdl	File on which to operate
 * @param[in]     bypass	If state doesn't indicate a share reservation,
 *				bypass any deny read
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 *
 * @return Nothing; results are in callback
 */

void vn_read2(struct fsal_obj_handle* obj_hdl,
	bool bypass,
	fsal_async_cb done_cb,
	struct fsal_io_arg* read_arg,
	void* caller_arg)
{
	struct vn_fsal_obj_handle* myself = container_of(obj_hdl,
		struct vn_fsal_obj_handle, obj_handle);
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct vn_fsal_export* vn_export =
		container_of(op_ctx->fsal_export, struct vn_fsal_export, m_export);

	if (read_arg->info != NULL) {
		/* Currently we don't support READ_PLUS */
		done_cb(obj_hdl, fsalstat(ERR_FSAL_NOTSUPP, 0), read_arg,
			caller_arg);
		return;
	}

	size_t nb_read = vn_readv(vn_export->mount_ctx, myself->vfile, read_arg->iov, read_arg->iov_count, read_arg->offset);

	if (read_arg->offset == -1 || nb_read == -1) {
		int retval = errno;
		status = fsalstat(posix2fsal_error(retval), retval);
		goto out;
	}

	read_arg->io_amount = nb_read;

	read_arg->end_of_file = (nb_read == 0);


	done_cb(obj_hdl, fsalstat(ERR_FSAL_NO_ERROR, 0), read_arg, caller_arg);
out:
	return;
}

/**
 * @brief Write data to a file
 *
 * This function writes data to a file. The FSAL must be able to
 * perform the write whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations. Even
 * with bypass == true, it will enforce a mandatory (NFSv4) deny_write if
 * an appropriate state is not passed).
 *
 * The FSAL is expected to enforce sync if necessary.
 *
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any non-mandatory deny write
 * @param[in,out] done_cb	Callback to call when I/O is done
 * @param[in,out] read_arg	Info about read, passed back in callback
 * @param[in,out] caller_arg	Opaque arg from the caller for callback
 */

void vn_write2(struct fsal_obj_handle* obj_hdl,
	bool bypass,
	fsal_async_cb done_cb,
	struct fsal_io_arg* write_arg,
	void* caller_arg)
{
	ssize_t nb_written;
	fsal_status_t status;
	int retval = 0;
	struct vn_fsal_obj_handle* myself = container_of(obj_hdl,
		struct vn_fsal_obj_handle, obj_handle);

	if (obj_hdl->fsal != obj_hdl->fs->fsal) {
		LogDebug(COMPONENT_FSAL,
			"FSAL %s operation for handle belonging to FSAL %s, return EXDEV",
			obj_hdl->fsal->name, obj_hdl->fs->fsal->name);
		done_cb(obj_hdl, fsalstat(posix2fsal_error(EXDEV), EXDEV),
			write_arg, caller_arg);
		return;
	}


	/* Get a usable file descriptor */
	LogFullDebug(COMPONENT_FSAL, "Calling find_fd, state = %p",
		write_arg->state);
	


	nb_written = vn_writev(myself->mfo_exp->mount_ctx, myself->vfile, write_arg->iov, write_arg->iov_count,
		write_arg->offset);

	if (nb_written == -1) {
		retval = errno;
		status = fsalstat(posix2fsal_error(retval), retval);
		goto out;
	}

	write_arg->io_amount = nb_written;

	if (write_arg->fsal_stable) {
		retval = vn_fsync(myself->mfo_exp->mount_ctx, myself->vfile);
		if (retval == -1) {
			retval = errno;
			status = fsalstat(posix2fsal_error(retval), retval);
			write_arg->fsal_stable = false;
		}
	}

out:

	done_cb(obj_hdl, status, write_arg, caller_arg);
}

/**
 * @brief Commit written data
 *
 * This function flushes possibly buffered data to a file. This method
 * differs from commit due to the need to interact with share reservations
 * and the fact that the FSAL manages the state of "file descriptors". The
 * FSAL must be able to perform this operation without being passed a specific
 * state.
 *
 * @param[in] obj_hdl          File on which to operate
 * @param[in] state            state_t to use for this operation
 * @param[in] offset           Start of range to commit
 * @param[in] len              Length of range to commit
 *
 * @return FSAL status.
 */

fsal_status_t vn_commit2(struct fsal_obj_handle* obj_hdl,
	off_t offset,
	size_t len)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}



/**
 * @brief Manage closing a file when a state is no longer needed.
 *
 * When the upper layers are ready to dispense with a state, this method is
 * called to allow the FSAL to close any file descriptors or release any other
 * resources associated with the state. A call to free_state should be assumed
 * to follow soon.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 *
 * @return FSAL status.
 */

fsal_status_t vn_close2(struct fsal_obj_handle* obj_hdl,
	struct state_t* state)
{
	struct vn_fsal_obj_handle* myself = container_of(obj_hdl,
		struct vn_fsal_obj_handle, obj_handle);

#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_close, __func__, __LINE__, obj_hdl,
		myself->m_name, state);
#endif


	int rc = vn_close_file(myself->mfo_exp->mount_ctx, myself->vfile);
	if(rc != 0 ){
		return fsalstat(ERR_FSAL_IO, -rc);
	}
	myself->vfile = NULL;


	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Get the wire version of a handle
 *
 * fill in the opaque f/s file handle part.
 * we zero the buffer to length first.  This MAY already be done above
 * at which point, remove memset here because the caller is zeroing
 * the whole struct.
 *
 * @param[in] obj_hdl	Handle to digest
 * @param[in] out_type	Type of digest to get
 * @param[out] fh_desc	Buffer to write digest into
 * @return FSAL status
 */
static fsal_status_t vn_handle_to_wire(const struct fsal_obj_handle* obj_hdl,
	fsal_digesttype_t output_type,
	struct gsh_buffdesc* fh_desc)
{
	const struct vn_fsal_obj_handle* myself;

	myself = container_of(obj_hdl,
		const struct vn_fsal_obj_handle,
		obj_handle);

	switch (output_type) {
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < V4_FH_OPAQUE_SIZE) {
			LogMajor(COMPONENT_FSAL,
				"Space too small for handle.  need %lu, have %zu",
				((unsigned long)V4_FH_OPAQUE_SIZE),
				fh_desc->len);
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		}

		memcpy(fh_desc->addr, myself->handle, V4_FH_OPAQUE_SIZE);
		fh_desc->len = V4_FH_OPAQUE_SIZE;
		break;

	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Get the unique key for a handle
 *
 * return a handle descriptor into the handle in this object handle
 * @TODO reminder.  make sure things like hash keys don't point here
 * after the handle is released.
 *
 * @param[in] obj_hdl	Handle to digest
 * @param[out] fh_desc	Buffer to write key into
 */
static void vn_handle_to_key(struct fsal_obj_handle* obj_hdl,
	struct gsh_buffdesc* fh_desc)
{
	struct vn_fsal_obj_handle* myself;

	myself = container_of(obj_hdl,
		struct vn_fsal_obj_handle,
		obj_handle);

	fh_desc->addr = myself->handle;
	fh_desc->len = V4_FH_OPAQUE_SIZE;
}

/**
 * @brief Get a ref on a MEM handle
 *
 * Stub, for bypass in unit tests
 *
 * @param[in] obj_hdl	Handle to ref
 */
static void vn_get_ref(struct fsal_obj_handle* obj_hdl)
{
	struct vn_fsal_obj_handle* myself;

	myself = container_of(obj_hdl,
		struct vn_fsal_obj_handle,
		obj_handle);
	vn_int_get_ref(myself);
}

/**
 * @brief Put a ref on a MEM handle
 *
 * Stub, for bypass in unit tests
 *
 * @param[in] obj_hdl	Handle to unref
 */
static void vn_put_ref(struct fsal_obj_handle* obj_hdl)
{
	struct vn_fsal_obj_handle* myself;

	myself = container_of(obj_hdl,
		struct vn_fsal_obj_handle,
		obj_handle);
	vn_int_put_ref(myself);
}

/**
 * @brief Release an object handle
 *
 * @param[in] obj_hdl	Handle to release
 */
static void vn_release(struct fsal_obj_handle* obj_hdl)
{
	struct vn_fsal_obj_handle* myself;

	myself = container_of(obj_hdl,
		struct vn_fsal_obj_handle,
		obj_handle);
	S5LOG_DEBUG("vn_release handle: %p", myself);

	vn_int_put_ref(myself);
}

/**
 * @brief Merge two handles
 *
 * For a failed create, we need to merge the two handles.  If the handles are
 * the same, we need to ref the handle, so that the following release doesn't
 * free it.
 *
 * @param[in] old_hdl	Handle to merge
 * @param[in] new_hdl	Handle to merge
 */
static fsal_status_t vn_merge(struct fsal_obj_handle* old_hdl,
	struct fsal_obj_handle* new_hdl)
{
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };

	if (old_hdl == new_hdl) {
		/* Nothing to merge */
		return status;
	}

	if (old_hdl->type == REGULAR_FILE &&
		new_hdl->type == REGULAR_FILE) {
		/* We need to merge the share reservations on this file.
		 * This could result in ERR_FSAL_SHARE_DENIED.
		 */
		struct vn_fsal_obj_handle* old, * newh;

		old = container_of(old_hdl,
			struct vn_fsal_obj_handle,
			obj_handle);
		newh = container_of(new_hdl,
			struct vn_fsal_obj_handle,
			obj_handle);

		/* This can block over an I/O operation. */
		status = merge_share(old_hdl, &old->share,
			&newh->share);
	}

	return status;
}

void vn_handle_ops_init(struct fsal_obj_ops* ops)
{
	fsal_default_obj_ops_init(ops);

	ops->get_ref = vn_get_ref;
	ops->put_ref = vn_put_ref;
	ops->merge = vn_merge;
	ops->release = vn_release;
	ops->lookup = vn_lookup;
	ops->readdir = vn_readdir;
	ops->mkdir = vn_mkdir;
	ops->mknode = vn_mknode;
	ops->symlink = vn_symlink;
	ops->readlink = vn_readlink;
	ops->getattrs = vn_getattrs;
	ops->setattr2 = vn_setattr2;
	ops->link = vn_link;
	ops->rename = vn_rename;
	ops->unlink = vn_unlink;
	ops->close = vn_close;
	ops->open2 = vn_open2;
	//ops->reopen2 = vn_reopen2;
	ops->read2 = vn_read2;
	ops->write2 = vn_write2;
	ops->commit2 = vn_commit2;
	//ops->lock_op2 = vn_lock_op2;
	ops->close2 = vn_close2;
	ops->handle_to_wire = vn_handle_to_wire;
	ops->handle_to_key = vn_handle_to_key;
}

/* export methods that create object handles
 */

 /* lookup_path
  * modelled on old api except we don't stuff attributes.
  * KISS
  * use as export_ops.  handle_ops will use vn_lookup
  */

fsal_status_t vn_lookup_path(struct fsal_export* exp_hdl,
	const char* path,
	struct fsal_obj_handle** obj_hdl,
	struct fsal_attrlist* attrs_out)
{
	struct vn_fsal_export* mfe;

	mfe = container_of(exp_hdl, struct vn_fsal_export, m_export);
	S5LOG_DEBUG(" vn_lookup_path for %s", path);
	if (strcmp(path, mfe->export_path) != 0) {
		/* Lookup of a path other than the export's root. */
		LogCrit(COMPONENT_FSAL,
			"Attempt to lookup non-root path %s",
			path);
		return fsalstat(ERR_FSAL_NOENT, ENOENT);
	}

	struct vn_fsal_obj_handle* hdl  = vn_alloc_handle(NULL, mfe->export_path, &mfe->mount_ctx->root_inode, mfe);
	
	
	*obj_hdl = &hdl->obj_handle;
	//export_root_object_get(*obj_hdl);

	if (attrs_out != NULL)
		fsal_copy_attrs(attrs_out, &hdl->attrs, false);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* create_handle
 * Does what original FSAL_ExpandHandle did (sort of)
 * returns a ref counted handle to be later used in mdcache etc.
 * NOTE! you must release this thing when done with it!
 * BEWARE! Thanks to some holes in the *AT syscalls implementation,
 * we cannot get an fd on an AF_UNIX socket, nor reliably on block or
 * character special devices.  Sorry, it just doesn't...
 * we could if we had the handle of the dir it is in, but this method
 * is for getting handles off the wire for cache entries that have LRU'd.
 * Ideas and/or clever hacks are welcome...
 * 
 * this function is used as export_ops
 */

fsal_status_t vn_create_handle(struct fsal_export* exp_hdl,
	struct gsh_buffdesc* hdl_desc,
	struct fsal_obj_handle** obj_hdl,
	struct fsal_attrlist* attrs_out)
{
	struct glist_head* glist;
	struct fsal_obj_handle* hdl;
	struct vn_fsal_obj_handle* my_hdl;

	*obj_hdl = NULL;

	if (hdl_desc->len != V4_FH_OPAQUE_SIZE) {
		LogCrit(COMPONENT_FSAL,
			"Invalid handle size %zu expected %lu",
			hdl_desc->len,
			((unsigned long)V4_FH_OPAQUE_SIZE));

		return fsalstat(ERR_FSAL_BADHANDLE, 0);
	}

	PTHREAD_RWLOCK_rdlock(&exp_hdl->fsal->lock);

	glist_for_each(glist, &exp_hdl->fsal->handles) {
		hdl = glist_entry(glist, struct fsal_obj_handle, handles);

		my_hdl = container_of(hdl,
			struct vn_fsal_obj_handle,
			obj_handle);

		if (memcmp(my_hdl->handle,
			hdl_desc->addr,
			V4_FH_OPAQUE_SIZE) == 0) {
			LogDebug(COMPONENT_FSAL,
				"Found hdl=%p name=%s",
				my_hdl, my_hdl->name.c_str());

#ifdef USE_LTTNG
			tracepoint(fsalmem, vn_create_handle, __func__,
				__LINE__, hdl, my_hdl->m_name);
#endif
			* obj_hdl = hdl;

			PTHREAD_RWLOCK_unlock(&exp_hdl->fsal->lock);

			if (attrs_out != NULL) {
				fsal_copy_attrs(attrs_out, &my_hdl->attrs,
					false);
			}

			return fsalstat(ERR_FSAL_NO_ERROR, 0);
		}
	}

	LogDebug(COMPONENT_FSAL,
		"Could not find handle");

	PTHREAD_RWLOCK_unlock(&exp_hdl->fsal->lock);

	return fsalstat(ERR_FSAL_STALE, ESTALE);
}
