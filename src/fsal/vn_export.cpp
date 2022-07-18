

#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <dlfcn.h>


#include "vn_fsal.h"
#include "vivenas.h"
#include "pf_utils.h"
extern "C" {
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "nfs_exports.h"
#include "nfs_core.h"
#include "export_mgr.h"
#ifdef USE_LTTNG
#include "gsh_lttng/fsal_mem.h"
#endif
}

extern "C" void vn_release_export(struct fsal_export* exp_hdl)
{
	struct vn_fsal_export* myself;
	myself = container_of(exp_hdl, struct vn_fsal_export, m_export);
	S5LOG_INFO("vn_release_export, :%p", myself);

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	glist_del(&myself->export_entry);

	gsh_free(myself->export_path);
	gsh_free(myself);
}

static fsal_status_t vn_get_dynamic_info(struct fsal_export* exp_hdl,
	struct fsal_obj_handle* obj_hdl,
	fsal_dynamicfsinfo_t* infop)
{
	infop->total_bytes = 0;
	infop->free_bytes = 0;
	infop->avail_bytes = 0;
	infop->total_files = 0;
	infop->free_files = 0;
	infop->avail_files = 0;
	infop->time_delta.tv_sec = 0;
	infop->time_delta.tv_nsec = FSAL_DEFAULT_TIME_DELTA_NSEC;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t vn_wire_to_host(struct fsal_export* exp_hdl,
	fsal_digesttype_t in_type,
	struct gsh_buffdesc* fh_desc,
	int flags)
{
	size_t fh_min;
	uint64_t* hashkey;
	ushort* len;

	fh_min = 1;

	if (fh_desc->len < fh_min) {
		LogMajor(COMPONENT_FSAL,
			"Size mismatch for handle.  should be >= %zu, got %zu",
			fh_min, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	hashkey = (uint64_t*)fh_desc->addr;
	len = (ushort*)((char*)hashkey + sizeof(uint64_t));
	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		* len = bswap_16(*len);
		*hashkey = bswap_64(*hashkey);
#endif
	}
	else {
#if (BYTE_ORDER == BIG_ENDIAN)
		* len = bswap_16(*len);
		*hashkey = bswap_64(*hashkey);
#endif
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */

static struct state_t* vn_alloc_state(struct fsal_export* exp_hdl,
	enum state_type state_type,
	struct state_t* related_state)
{
	struct state_t* state;
	struct vn_fd* my_fd;
	struct vn_state_fd* fdstat = (struct vn_state_fd* )gsh_calloc(1, sizeof(struct vn_state_fd));
	state = init_state(&fdstat->state,
		exp_hdl, state_type, related_state);

	my_fd = &container_of(state, struct vn_state_fd, state)->fd;

	my_fd->vf = NULL;
	my_fd->openflags = FSAL_O_CLOSED;
	PTHREAD_RWLOCK_init(&my_fd->fdlock, NULL);

	return state;
}

/* vn_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void vn_export_ops_init(struct export_ops* ops)
{
	ops->release = vn_release_export;
	ops->lookup_path = vn_lookup_path;
	ops->wire_to_host = vn_wire_to_host;
	ops->create_handle = vn_create_handle;
	ops->get_fs_dynamic_info = vn_get_dynamic_info;
	ops->alloc_state = vn_alloc_state;
}

const char* str_async_type(uint32_t async_type)
{
	switch (async_type) {
	case MEM_INLINE:
		return "INLINE";
	case MEM_RANDOM_OR_INLINE:
		return "RANDOM_OR_INLINE";
	case MEM_RANDOM:
		return "RANDOM";
	case MEM_FIXED:
		return "FIXED";
	}

	return "UNKNOWN";
}

//static struct config_item_list async_types_conf[] = {
//	CONFIG_LIST_TOK("inline",		MEM_INLINE),
//	CONFIG_LIST_TOK("fixed",		MEM_FIXED),
//	CONFIG_LIST_TOK("random",		MEM_RANDOM),
//	CONFIG_LIST_TOK("random_or_inline",	MEM_RANDOM_OR_INLINE),
//	CONFIG_LIST_EOL
//};
#define CONF_ITEM_STR_CPP(_name_, _minsize_, _maxsize_, _def_, _struct_, _mem_) \
	{ name : _name_,			    \
	  type : CONFIG_STRING,		    \
	  u:{str:{minsize : _minsize_,		    \
	          maxsize : _maxsize_,		    \
	          def : _def_}},			    \
	  off : offsetof(struct _struct_, _mem_)   \
	}


static struct config_item vn_export_params[] = {
	CONF_ITEM_NOOP((char*)"name"),
	CONF_ITEM_STR_CPP((char*)"user_id", 0, 64, NULL, vn_fsal_export, user_id),
	CONF_ITEM_STR_CPP((char*)"db_path", 0, 256, NULL, vn_fsal_export, db_path),
	CONFIG_EOL
};

static struct config_block vn_export_param_block = {
	dbus_interface_name : (char*)"org.ganesha.nfsd.config.fsal.vivenas-export%d",
	blk_desc : {name: (char*)"FSAL",
				type : CONFIG_BLOCK,
				u : {blk: {init: noop_conf_init,
					       params : vn_export_params,
					       commit : noop_conf_commit}}}
};

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t vn_create_export(struct fsal_module* fsal_hdl,
	void* parse_node,
	struct config_error_type* err_type,
	const struct fsal_up_vector* up_ops)
{
	struct vn_fsal_export* myself;
	int retval = 0;
	pthread_rwlockattr_t attrs;
	fsal_status_t fsal_status = { (fsal_errors_t)0, 0 };
	Cleaner _c;

	myself = (struct vn_fsal_export*)gsh_calloc(1, sizeof(struct vn_fsal_export));
	_c.push_back([myself] {gsh_free(myself); });
	glist_init(&myself->mfe_objs);
	pthread_rwlockattr_init(&attrs);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(&attrs,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	PTHREAD_RWLOCK_init(&myself->mfe_exp_lock, &attrs);
	pthread_rwlockattr_destroy(&attrs);
	fsal_export_init(&myself->m_export);
	myself->m_export.fsal = fsal_hdl;
	myself->m_export.up_ops = up_ops;


	vn_export_ops_init(&myself->m_export.exp_ops);
	_c.push_back([myself]() {free_export_ops(&myself->m_export); });
	retval = load_config_from_node(parse_node,
		&vn_export_param_block,
		myself,
		true,
		err_type);

	if (retval != 0) {
		fsal_status = posix2fsal_status(EINVAL);
		return fsal_status;	/* seriously bad */

	}
	S5LOG_INFO("Mount vivefs:%s", myself->db_path);
	myself->mount_ctx = vn_mount(myself->db_path);
	if(myself->mount_ctx == NULL){
		fsal_status = posix2fsal_status(EIO);
		return fsal_status;	/* seriously bad */

	}
	myself->root = myself->mount_ctx->root_file.get();

	/* Save the export path. */
	myself->export_path = gsh_strdup(CTX_FULLPATH(op_ctx));
	op_ctx->fsal_export = &myself->m_export;

	/* Insert into exports list */
	glist_add_tail(&ViveNASM.vn_exports, &myself->export_entry);

	LogDebug(COMPONENT_FSAL,
		"Created exp %p - %s",
		myself, myself->export_path);

	assert(atomic_fetch_int32_t(&fsal_hdl->refcount) > 0);
	retval = fsal_attach_export(fsal_hdl, &myself->m_export.exports);
	if (retval != 0) {
		/* seriously bad */
		LogMajor(COMPONENT_FSAL,
			"Could not attach export");
		fsal_status = posix2fsal_status(retval);
		return fsal_status;	/* seriously bad */
	}

	S5LOG_INFO("vn_create_export :%p", myself);
	_c.cancel_all();
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

/**
 * @brief Update an existing export
 *
 * This will result in a temporary fsal_export being created, and built into
 * a stacked export.
 *
 * On entry, op_ctx has the original gsh_export and no fsal_export.
 *
 * The caller passes the original fsal_export, as well as the new super_export's
 * FSAL when there is a stacked export. This will allow the underlying export to
 * validate that the stacking has not changed.
 *
 * This function does not actually create a new fsal_export, the only purpose is
 * to validate and update the config.
 *
 * @param[in]     fsal_hdl         FSAL module
 * @param[in]     parse_node       opaque pointer to parse tree node for
 *                                 export options to be passed to
 *                                 load_config_from_node
 * @param[out]    err_type         config processing error reporting
 * @param[in]     original         The original export that is being updated
 * @param[in]     updated_super    The updated super_export's FSAL
 *
 * @return FSAL status.
 */

fsal_status_t vn_update_export(struct fsal_module* fsal_hdl,
	void* parse_node,
	struct config_error_type* err_type,
	struct fsal_export* original,
	struct fsal_module* updated_super)
{
	struct vn_fsal_export myself;
	int retval = 0;
	struct vn_fsal_export* orig =
		container_of(original, struct vn_fsal_export, m_export);
	fsal_status_t status;

	/* Check for changes in stacking by calling default update_export. */
	status = update_export(fsal_hdl, parse_node, err_type,
		original, updated_super);

	if (FSAL_IS_ERROR(status))
		return status;

	memset(&myself, 0, sizeof(myself));

	retval = load_config_from_node(parse_node,
		&vn_export_param_block,
		&myself,
		true,
		err_type);

	if (retval != 0) {
		return posix2fsal_status(EINVAL);
	}

	/* Update the async parameters */
	atomic_store_uint32_t(&orig->async_delay, myself.async_delay);
	atomic_store_uint32_t(&orig->async_stall_delay,
		myself.async_stall_delay);
	atomic_store_uint32_t(&orig->async_type, myself.async_type);

	LogEvent(COMPONENT_FSAL,
		"Updated FSAL_MEM aync parameters type=%s, delay=%" PRIu32
		", stall_delay=%" PRIu32,
		str_async_type(myself.async_type),
		myself.async_delay, myself.async_stall_delay);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
