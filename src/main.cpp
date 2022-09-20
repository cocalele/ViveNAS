// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/convenience.h"
#include <rocksdb/utilities/object_registry.h>
#include "pfaof.h"
#include "vivenas.h"
#include "vivenas_internal.h"
#include "pf_log.h"
#include "data_merge.h"

#include "../PureFlash/common/include/pf_utils.h"
#include <gsh_list.h>


using namespace ROCKSDB_NAMESPACE;

using ROCKSDB_NAMESPACE::DB;
using ROCKSDB_NAMESPACE::Options;
using ROCKSDB_NAMESPACE::PinnableSlice;
using ROCKSDB_NAMESPACE::ReadOptions;
using ROCKSDB_NAMESPACE::Status;
using ROCKSDB_NAMESPACE::WriteBatch;
using ROCKSDB_NAMESPACE::WriteOptions;
using ROCKSDB_NAMESPACE::Env;
using ROCKSDB_NAMESPACE::ConfigOptions;

using  namespace nlohmann; 
using namespace std;
static std::shared_ptr<ROCKSDB_NAMESPACE::Env> env_guard;
static ROCKSDB_NAMESPACE::Env* FLAGS_env = ROCKSDB_NAMESPACE::Env::Default();
static ViveFsContext* g_fs_ctx;


void to_json(json& j, const ViveSuperBlock& r)
{
	j = json{ { "magic", r.magic },{ "version", r.version } };
}
void from_json(const json& j, ViveSuperBlock& s)
{
	j.at("magic").get_to(s.magic);
	j.at("version").get_to(s.version);

}
void deserialize_superblock(const char* buf, ViveSuperBlock& sb)
{
	json j = json::parse(buf);
	j.get_to(sb);

}
string serialize_superblock(const ViveSuperBlock& sb)
{
	json jsb = sb;
	return jsb.dump();
}

ViveFsContext* vn_mount(const char* db_path) {
	static_assert(sizeof(struct ViveInode) == VIVEFS_INODE_SIZE, "ViveInode size error");
	Options options;
	ConfigOptions config_options;
	S5LOG_INFO("Mounting ViveFS:%s in thread:%d ...", db_path, gettid());
	ViveFsContext* ctx = new ViveFsContext();
	Cleaner _c;
	_c.push_back([ctx]() {delete ctx; });
	Status s =
		Env::CreateFromUri(config_options, "", "pfaof", &FLAGS_env, &env_guard);
	if (!s.ok()) {
		S5LOG_ERROR("Failed creating env: %s\n", s.ToString().c_str());
		return NULL;
	}


	// Optimize RocksDB. This is the easiest way to get RocksDB to perform well
	options.IncreaseParallelism();
	options.OptimizeLevelStyleCompaction();
	// create the DB if it's not already present
	options.create_if_missing = true;
	options.env = FLAGS_env;
	TransactionDBOptions tx_opt;


	// open DB with two column families
	std::vector<ColumnFamilyDescriptor> column_families;
	// have to open default column family
	column_families.push_back(ColumnFamilyDescriptor(
		ROCKSDB_NAMESPACE::kDefaultColumnFamilyName, ColumnFamilyOptions()));
	// open the new one, too
	column_families.push_back(ColumnFamilyDescriptor(
		"meta_cf", ColumnFamilyOptions()));
	ColumnFamilyOptions data_cf_opt;
	data_cf_opt.merge_operator.reset(new ViveDataMergeOperator());
	column_families.push_back(ColumnFamilyDescriptor(
		"data_cf", data_cf_opt));
	std::vector<ColumnFamilyHandle*> handles;
	ctx->db_path = db_path;
	options.manual_wal_flush = true;
	s = TransactionDB::Open(options, tx_opt, ctx->db_path.c_str(), column_families, &handles, &ctx->db);
	if(!s.ok()){
		S5LOG_ERROR( "Failed open db:%s, %s", ctx->db_path.c_str(), s.ToString().c_str());
		return NULL;
	} else {
		S5LOG_INFO("Succeed open db:%s, ptr %p", ctx->db_path.c_str(), ctx->db);
	}
	ctx->default_cf = handles[0];
	ctx->meta_cf = handles[1];
	ctx->data_cf = handles[2];

	ctx->meta_opt.sync = true;

	//not work, data still lost, it's problem of merge
	//ctx->data_opt.sync = true; //to test auto flush
  
	memset(&ctx->root_inode, 0, sizeof(ctx->root_inode));
	ctx->root_inode.i_no = VN_ROOT_INO;
	ctx->root_inode.i_mode = S_IFDIR|00777; //NOTE: S_IFDIR  0040000 is defined in octet, not hex number
	ctx->root_inode.i_links_count = 2;
	ctx->root_inode.i_size = 4096;
	ctx->root_inode.i_atime = ctx->root_inode.i_ctime = time(NULL);

	string v;
	s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, INODE_SEED_KEY, &v);
	if (!s.ok()) {
		return NULL;
	}
	ctx->inode_seed = deserialize_int64(v.data());
	s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, "vivefs_sb", &v);
	if (!s.ok()) {
		return NULL;
	}

	ViveSuperBlock sb;
	deserialize_superblock(v.data(), sb);
	if (sb.magic != VIVEFS_MAGIC_STR) {
		S5LOG_ERROR("Not a ViveNAS file system");
		return NULL;
	}
	if (sb.version != VIVEFS_VER) {
		S5LOG_ERROR("ViveNAS version not match");
		return NULL;
	}

	ViveFile* f = new ViveFile;
	if (f == NULL) {
		S5LOG_ERROR("Failed alloc memory for ViveFile");
		return NULL;
	}
	f->i_no = VN_ROOT_INO;
	f->inode = &ctx->root_inode;
	f->file_name = "/";
	ctx->root_file.reset(f);


	_c.cancel_all();
	S5LOG_INFO("Mount ViveFS:%s succeed", db_path);
	g_fs_ctx = ctx;
	return ctx;
}

int vn_umount(ViveFsContext* ctx)
{
	S5LOG_INFO("umounting ViveFS:%s in thread:%d", ctx->db_path.c_str(), gettid());
 
	std::vector<ColumnFamilyHandle*> cfs;
	cfs.push_back(ctx->meta_cf);
	cfs.push_back(ctx->data_cf);
	cfs.push_back(ctx->default_cf);

	//TODO: the following Get may cause segfault. reason still unknown
	PinnableSlice v;
	Status s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, INODE_SEED_KEY, &v);
	if (!s.ok()) {
		S5LOG_ERROR("Failed GET key:%s, %s", INODE_SEED_KEY, s.ToString().c_str());
	}
	else {
		fprintf(stderr, "Succeed get key:%s\n", INODE_SEED_KEY);
	}
	ctx->db->Flush(FlushOptions(), cfs);
	ctx->db->SyncWAL();
	CancelAllBackgroundWork(ctx->db, true);
	//delete ctx->db; //not need here, db will be delete in ~ViveFsContext
	g_fs_ctx = NULL;
	delete ctx;
	return 0;
}
ViveFsContext::ViveFsContext() : db(NULL),default_cf(NULL),meta_cf(NULL),data_cf(NULL)
{
	meta_opt = WriteOptions();
	meta_opt.sync = false;
	data_opt = WriteOptions();
	data_opt.sync = false;
	read_opt = ReadOptions();
}
int64_t ViveFsContext::generate_inode_no()
{
	return inode_seed++;
}
ViveFsContext::~ViveFsContext()
{
	db->FlushWAL(true);
	db->DestroyColumnFamilyHandle(meta_cf);
	db->DestroyColumnFamilyHandle(data_cf);
	db->DestroyColumnFamilyHandle(default_cf);
	db->Close();

	delete db;
	db=NULL;
}

int vn_flush_fs(struct ViveFsContext* ctx)
{
	std::vector<ColumnFamilyHandle*> cfs;

	cfs.push_back(ctx->meta_cf);
	cfs.push_back(ctx->data_cf);
	cfs.push_back(ctx->default_cf);


	S5LOG_INFO("Flushing db:%s", ctx->db_path.c_str());
	Status s = ctx->db->Flush(FlushOptions(), cfs);
	if(!s.ok()){
		S5LOG_ERROR("Failed flush db:%s, for:%s", ctx->db_path.c_str(), s.ToString());
		return -EIO;
	}
	s = ctx->db->SyncWAL();
	if (!s.ok()) {
		S5LOG_ERROR("Failed SyncWAL db:%s, for:%s", ctx->db_path.c_str(), s.ToString());
		return -EIO;
	}
	CancelAllBackgroundWork(ctx->db, true);
	return 0;
}

struct ViveInode* vn_get_root_inode(struct ViveFsContext* ctx)
{
	return &ctx->root_inode;
}

