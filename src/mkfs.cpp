#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/convenience.h"
#include "vivenas.h"
#include <nlohmann/json.hpp>
#include <string>
#include "pf_utils.h"
#include "vivenas.h"

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

using nlohmann::json;
using namespace std;
#define VIVEFS_MAGIC_STR "vivefs_0"
#define VIVEFS_VER 0x00010000

static void to_json(json& j, const ViveSuperBlock& r)
{
	j = json{ { "magic", r.magic },{ "version", r.version } };
}

int vn_mkfs_vivefs(const char* db_path)
{
	DB* db;
	Options options;
	ConfigOptions config_options;

	Status s =
		Env::CreateFromUri(config_options, "", "pfaof", &FLAGS_env, &env_guard);
	if (!s.ok()) {
		fprintf(stderr, "Failed creating env: %s\n", s.ToString().c_str());
		exit(1);
	}


	// Optimize RocksDB. This is the easiest way to get RocksDB to perform well
	options.IncreaseParallelism();
	options.OptimizeLevelStyleCompaction();
	// create the DB if it's not already present
	options.create_if_missing = true;
	options.env = FLAGS_env;


	// open DB
	s = DB::Open(options, db_path, &db);
	if (!s.ok()) {
		fprintf(stderr, "Failed to open database:%s, %s", db_path, s.ToString().c_str());
		exit(s.code());
	}
	DeferCall _1([db]() {delete db; });

	ColumnFamilyHandle* cf1;
	s = db->CreateColumnFamily(ColumnFamilyOptions(), "meta_cf", &cf1);
	if (!s.ok()) {
		fprintf(stderr, "Failed to create CF meta_cf, %s", s.ToString().c_str());
		exit(s.code());
	}
	
	ColumnFamilyHandle* cf2;
	s = db->CreateColumnFamily(ColumnFamilyOptions(), "data_cf", &cf2);
	if (!s.ok()) {
		fprintf(stderr, "Failed to create CF data_cf, %s", s.ToString().c_str());
		exit(s.code());
	}

	ViveSuperBlock sb = { VIVEFS_MAGIC_STR , VIVEFS_VER };
	json jsb = sb;
	db->Put(WriteOptions(), "vivefs_sb", jsb.dump());

	CHECKED_CALL(db->DestroyColumnFamilyHandle(cf1));
	CHECKED_CALL(db->DestroyColumnFamilyHandle(cf2));
	CHECKED_CALL(db->SyncWAL());
	CHECKED_CALL(db->Close());
	delete db;
}

