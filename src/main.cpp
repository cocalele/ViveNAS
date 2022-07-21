// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>
#include <fcntl.h>
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
   
    Options options;
    ConfigOptions config_options;
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
    }
    ctx->default_cf = handles[0];
    ctx->meta_cf = handles[1];
    ctx->data_cf = handles[2];

    ctx->meta_opt.sync = true;
  
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
    return ctx;
}

int umount(ViveFsContext* ctx)
{
    S5LOG_INFO("umountint ViveFS:%s", ctx->db_path.c_str());
    delete ctx->db;
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
	db->DestroyColumnFamilyHandle(meta_cf);
	db->DestroyColumnFamilyHandle(data_cf);
	db->FlushWAL(true);
	db->Close();

    delete db;
}


extern "C" void vn_say_hello(const char* s)
{
    S5LOG_INFO("Message is:%s", s);
}
