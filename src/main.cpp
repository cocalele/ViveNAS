// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <cstdio>
#include <string>

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

#if defined(OS_WIN)
std::string kDBPath = "C:\\Windows\\TEMP\\rocksdb_simple_example";
#else
std::string kDBPath = "/rocksdb_simple_example";
#endif
static std::shared_ptr<ROCKSDB_NAMESPACE::Env> env_guard;
static ROCKSDB_NAMESPACE::Env* FLAGS_env = ROCKSDB_NAMESPACE::Env::Default();
void __PfAof_init();
int main() {
	printf("Hello, I'm example\n");
	__PfAof_init();
    return 0;
}
ViveFsContext* vn_mount(const char* db_path) {
   
    Options options;
    ConfigOptions config_options;
    ViveFsContext* ctx = new ViveFsContext();
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
    s = TransactionDB::Open(options, tx_opt, ctx->db_path.c_str(), column_families, &handles, &ctx->db);
    if(!s.ok()){
        S5LOG_ERROR( "Failed open db:%s, %s", ctx->db_path.c_str(), s.ToString().c_str());
    }
    ctx->default_cf = handles[0];
    ctx->meta_cf = handles[1];
    ctx->data_cf = handles[2];

    ctx->meta_opt.sync = true;
  
    memset(&ctx->root_inode, 0, sizeof(ctx->root_inode));
    ctx->root_inode.i_no = VN_ROOT_INO;

    ViveFile* f = new ViveFile;
    f->i_no = VN_ROOT_INO;
    f->inode = &ctx->root_inode;
    f->file_name = "/";
    ctx->root_file.reset(f);


    return ctx;
}

int umount(ViveFsContext* ctx)
{
    S5LOG_INFO("umountint ViveFS:%s", ctx->db_path.c_str());
    delete ctx->db;
    delete ctx;
    return 0;
}