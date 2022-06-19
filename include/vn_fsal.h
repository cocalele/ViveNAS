#ifndef vn_fsal_h__
#define vn_fsal_h__
#include "vivenas.h"
extern "C" {
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"

#include "gsh_list.h"
#ifdef USE_LTTNG
#include "gsh_lttng/fsal_mem.h"
#endif
}

struct vn_fsal_obj_handle;
class ViveFile;

enum async_types {
	MEM_INLINE,
	MEM_RANDOM_OR_INLINE,
	MEM_RANDOM,
	MEM_FIXED,
};



fsal_status_t vn_lookup_path(struct fsal_export* exp_hdl,
	const char* path,
	struct fsal_obj_handle** handle,
	struct fsal_attrlist* attrs_out);

fsal_status_t vn_create_handle(struct fsal_export* exp_hdl,
	struct gsh_buffdesc* hdl_desc,
	struct fsal_obj_handle** handle,
	struct fsal_attrlist* attrs_out);

/*
 * MEM internal object handle
 */

#define V4_FH_OPAQUE_SIZE 58 /* Size of state_obj digest */

struct vn_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct fsal_attrlist attrs;
	vn_inode_no_t inode;
	char handle[V4_FH_OPAQUE_SIZE];
	union {
		struct {
			unsigned char* link_content;
			int link_size;
		} symlink;
		struct {
			ViveFile* vfile;
			struct fsal_share share;

		};
	} ;

	//struct fsal_share share;

	struct glist_head dirents; /**< List of dirents pointing to obj */
	struct glist_head mfo_exp_entry; /**< Link into mfs_objs */
	struct vn_fsal_export* mfo_exp; /**< Export owning object */
	//char* m_name;	/**< Base name of obj, for debugging */
	//uint32_t datasize;
	//bool is_export;
	int32_t refcount; /**< We persist handles, so we need a refcount */
	//char data[0]; /* Allocated data */
};

/**
 * @brief Dirent for FSAL_MEM
 */
struct vn_dirent {
	struct vn_fsal_obj_handle* hdl; /**< Handle dirent points to */
	struct vn_fsal_obj_handle* dir; /**< Dir containing dirent */
	const char* d_name;		 /**< Name of dirent */
	uint64_t d_index;		 /**< index in dir */
	struct avltree_node avl_n;	 /**< Entry in dir's avl_name tree */
	struct avltree_node avl_i;	 /**< Entry in dir's avl_index tree */
	struct glist_head dlist;	 /**< Entry in hdl's dirents list */
};

static inline bool vn_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
		|| (type == BLOCK_FILE)) {
		return true;
	}
	else {
		return false;
	}
}

void vn_handle_ops_init(struct fsal_obj_ops* ops);

/* Internal MEM method linkage to export object
*/

fsal_status_t vn_create_export(struct fsal_module* fsal_hdl,
	void* parse_node,
	struct config_error_type* err_type,
	const struct fsal_up_vector* up_ops);

fsal_status_t vn_update_export(struct fsal_module* fsal_hdl,
	void* parse_node,
	struct config_error_type* err_type,
	struct fsal_export* original,
	struct fsal_module* updated_super);

const char* str_async_type(uint32_t async_type);

#define vn_free_handle(h) _vn_free_handle(h, __func__, __LINE__)
/**
 * @brief Free a MEM handle
 *
 * @note mfe_exp_lock MUST be held for write
 * @param[in] hdl	Handle to free
 */
static inline void _vn_free_handle(struct vn_fsal_obj_handle* hdl,
	const char* func, int line)
{
#ifdef USE_LTTNG
	tracepoint(fsalmem, vn_free, func, line, hdl, hdl->m_name);
#endif

	glist_del(&hdl->mfo_exp_entry);
	hdl->mfo_exp = NULL;

	if (hdl->vfile != NULL) {
		delete hdl->vfile;
		hdl->vfile = NULL;
	}

	gsh_free(hdl);
}

void vn_clean_export(struct vn_fsal_obj_handle* root);
void vn_clean_all_dirents(struct vn_fsal_obj_handle* parent);

/**
 * @brief FSAL Module wrapper for MEM
 */
struct vivenas_fsal_module {
	/** Module we're wrapping */
	struct fsal_module fsal;
	/** fsal_obj_handle ops vector */
	struct fsal_obj_ops handle_ops;
	/** List of MEM exports. TODO Locking when we care */
	struct glist_head vn_exports;
	/** Config - size of data in inode */
	uint32_t inode_size;
	/** Config - Interval for UP call thread */
	uint32_t up_interval;
	/** Next unused inode */
	uint64_t next_inode;
	/** Config - number of async threads */
	uint32_t async_threads;
	/** Config - whether so use whence-is-name */
	bool whence_is_name;
	char* conf_path;
	struct glist_head  fs_obj; /* list of filesystem objects */
	pthread_mutex_t   lock; /* lock to protect above list */
};
extern struct vivenas_fsal_module ViveNASM;

/* ASYNC testing */
extern struct fridgethr* vn_async_fridge;

/* UP testing */
fsal_status_t vn_up_pkginit(void);
fsal_status_t vn_up_pkgshutdown(void);


struct vn_fsal_export {
	/** Export this wraps */
	struct fsal_export m_export;
	/** The path for this export */
	char* export_path;
	/** Root object for this export */
	struct vn_fsal_obj_handle* root_handle;
	struct ViveFsContext *mount_ctx;
	struct ViveFile* root;	/*< The root handle */
	char* user_id;			/* user_id for this mount */
	char* fs_name;			/* filesystem name */
	char* db_path;
	/** Entry into list of exports */
	struct glist_head export_entry;
	/** Lock protecting mfe_objs */
	pthread_rwlock_t mfe_exp_lock;
	/** List of all the objects in this export */
	struct glist_head mfe_objs;
	/** Async delay */
	uint32_t async_delay;
	/** Async Stall delay */
	uint32_t async_stall_delay;
	/** Type of async */
	uint32_t async_type;
};


void vn_export_ops_init(struct export_ops* ops);
ViveFsContext* vn_mount(const char* db_path);
#endif // vn_fsal_h__