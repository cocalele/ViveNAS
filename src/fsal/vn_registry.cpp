#include "vn_fsal.h"
extern "C" {
#include "fsal.h"
#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
}


//structed from ceph_fsal_module
struct vivenas_fsal_module ViveNASM = {
	.fsal = {
		.fs_info = {
		.maxfilesize = INT64_MAX,
			.maxlink = _POSIX_LINK_MAX,
			.maxnamelen = MAXNAMLEN,
			.maxpathlen = MAXPATHLEN,
			.no_trunc = true,
			.chown_restricted = true,
			.case_insensitive = false,
			.case_preserving = true,
			.link_support = true,
			.symlink_support = true,
			.lock_support = true,
			.lock_support_async_block = false,
			.named_attr = true,
			.unique_handles = true,
			.acl_support = FSAL_ACLSUPPORT_ALLOW |
							FSAL_ACLSUPPORT_DENY,
			.cansettime = true,
			.homogenous = true,
			.supported_attrs = ATTRS_POSIX,
			.maxread = 0,
			.maxwrite = 0,
			.umask = 0,
			.auth_exportpath_xdev = false,
			.delegations = FSAL_OPTION_FILE_DELEGATIONS,
			//.pnfs_mds = false,
			//.pnfs_ds = true,
			.link_supports_permission_checks = false,
			.readdir_plus = true,
			.expire_time_parent = -1,
		}
	}
};
static const char vnfsal_name[] = "ViveNAS";
#define CONF_ITEM_UI32_CPP(_name_, _min_, _max_, _def_, _struct_, _mem_) \
	{ name : _name_,			    \
	  type : CONFIG_UINT32,		    \
	  u:{ui32 :{minval : _min_,		    \
	  maxval : _max_,		    \
	  def : _def_,			    \
	  zero_ok : (_min_ == 0)}},	    \
	  off : offsetof(struct _struct_, _mem_)   \
	}
#define CONF_ITEM_BOOL_CPP(_name_, _def_, _struct_, _mem_) \
	{ name : _name_,			    \
	  type : CONFIG_BOOL,		    \
	  u:{b:{def : _def_}},			    \
	  off : offsetof(struct _struct_, _mem_)   \
	}
static struct config_item mem_items[] = {
	CONF_ITEM_UI32_CPP((char*)"Inode_Size", 0, 0x200000, 0,
			   vivenas_fsal_module, inode_size),

	CONFIG_EOL
};


static struct config_block mem_block = {
	dbus_interface_name : (char*)"org.ganesha.nfsd.config.fsal.vivenas",
	blk_desc : {name : (char*)"ViveNAS",
	type : CONFIG_BLOCK,
	u: { blk: {init : noop_conf_init,
		 params : mem_items,
		 commit : noop_conf_commit}}}
};
/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */
static fsal_status_t init_config(struct fsal_module* module_in,
	config_file_t config_struct,
	struct config_error_type* err_type)
{
	struct vivenas_fsal_module* myself =
		container_of(module_in, struct vivenas_fsal_module, fsal);

	LogDebug(COMPONENT_FSAL,
		"ViveNAS module setup.");

	(void)load_config_from_parse(config_struct,
		&mem_block,
		myself,
		true,
		err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	/* Initialize UP calls */
	fsal_status_t status = vn_up_pkginit();
	if (FSAL_IS_ERROR(status)) {
		LogMajor(COMPONENT_FSAL,
			"Failed to initialize FSAL_MEM UP package %s",
			fsal_err_txt(status));
		return status;
	}

	/* Set whence_is_name in fsinfo */
	myself->fsal.fs_info.whence_is_name = myself->whence_is_name;

	display_fsinfo(&myself->fsal);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
void 	__PfAof_init();

MODULE_INIT void vivenas_init(void)
{
	struct fsal_module* myself = &ViveNASM.fsal;
	__PfAof_init();

	if (register_fsal(myself, vnfsal_name, FSAL_MAJOR_VERSION,
		FSAL_MINOR_VERSION, FSAL_ID_VIVENAS) != 0) {
		LogCrit(COMPONENT_FSAL,
			"ViveNAS FSAL module failed to register.");
		return;
	}

	/* set up module operations */
	myself->m_ops.create_export = vn_create_export;

	/* setup global handle internals */
	myself->m_ops.init_config = init_config;
	/*
	 * Following inits needed for pNFS support
	 * get device info will used by pnfs meta data server
	 */
	//myself->m_ops.getdeviceinfo = getdeviceinfo;
	//myself->m_ops.fsal_pnfs_ds_ops = pnfs_ds_ops_init;

	
	vn_handle_ops_init(&ViveNASM.handle_ops);

	PTHREAD_MUTEX_init(&ViveNASM.lock, NULL);
	glist_init(&ViveNASM.fs_obj);
	glist_init(&ViveNASM.vn_exports);
	ViveNASM.inode_size = VIVEFS_INODE_SIZE;

	LogDebug(COMPONENT_FSAL, "FSAL ViveNAS initialized");
}

MODULE_FINI void glusterfs_unload(void)
{
	if (unregister_fsal(&ViveNASM.fsal) != 0) {
		LogCrit(COMPONENT_FSAL,
			"FSAL ViveNAS unable to unload.  Dying ...");
		return;
	}

	/* All the shares should have been unexported */
	//if (!glist_empty(&GlusterFS.fs_obj)) {
	//	LogWarn(COMPONENT_FSAL,
	//		"FSAL Gluster still contains active shares.");
	//}
	PTHREAD_MUTEX_destroy(&ViveNASM.lock);
	LogDebug(COMPONENT_FSAL, "FSAL ViveNAS unloaded");
}

