#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/convenience.h"
#include "vn_vivenas.h"
//#include <nlohmann/json.hpp>
#include <string>
#include "pf_utils.h"
#include "vn_vivenas.h"
#include "vn_vivenas_internal.h"

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

//using nlohmann::json;
using namespace std;
#define VIVEFS_MAGIC_STR "vivefs_0"
#define VIVEFS_VER 0x00010000



int vn_mkfs_vivefs(const char* db_path)
{
	Options options;
	ConfigOptions config_options;
	static std::shared_ptr<ROCKSDB_NAMESPACE::Env> env_guard;
	static ROCKSDB_NAMESPACE::Env* FLAGS_env = ROCKSDB_NAMESPACE::Env::Default();
	Status s =
		Env::CreateFromUri(config_options, "", "pfaof", &FLAGS_env, &env_guard);
	if (!s.ok()) {
		fprintf(stderr, "Failed creating env: %s\n", s.ToString().c_str());
		return -s.code();
	}

	setup_db_options(options);
	options.env = FLAGS_env;


	// open DB
	TransactionDBOptions tx_opt;
	ViveFsContext ctx;

	s = TransactionDB::Open(options, tx_opt, db_path, &ctx.db);
	if (!s.ok()) {
		fprintf(stderr, "Failed to open database:%s, %s", db_path, s.ToString().c_str());
		return -s.code();
	}
	
	ColumnFamilyOptions meta_opts, data_opts;
	setup_meta_cf_options(meta_opts);
	setup_data_cf_options(data_opts);

	s = ctx.db->CreateColumnFamily(meta_opts, "meta_cf", &ctx.meta_cf);
	if (!s.ok()) {
		fprintf(stderr, "Failed to create CF meta_cf, %s", s.ToString().c_str());
		return -s.code();
	}
	
	s = ctx.db->CreateColumnFamily(data_opts, "data_cf", &ctx.data_cf);
	if (!s.ok()) {
		fprintf(stderr, "Failed to create CF data_cf, %s", s.ToString().c_str());
		return -s.code();
	}
	Transaction* tx = ctx.db->BeginTransaction(ctx.meta_opt);
	DeferCall _2([tx]() {delete tx; });
	Cleaner _c;
	_c.push_back([tx]() {tx->Rollback(); });

	ctx.inode_seed = VN_FIRST_USER_INO;
	ViveSuperBlock sb = { VIVEFS_MAGIC_STR , VIVEFS_VER };
	CHECKED_CALL(tx->Put(ctx.meta_cf, "vivefs_sb", serialize_superblock(sb)));
	CHECKED_CALL(tx->Put(ctx.meta_cf, INODE_SEED_KEY, Slice((char*)&ctx.inode_seed, sizeof(ctx.inode_seed))));
	CHECKED_CALL(tx->Commit());

	std::vector<ColumnFamilyHandle*> cfs;
	//cfs.push_back(ctx.default_cf);
	cfs.push_back(ctx.meta_cf);
	cfs.push_back(ctx.data_cf);


	PinnableSlice v;
	s = ctx.db->Get(ReadOptions(), ctx.meta_cf, INODE_SEED_KEY, &v);
	if (!s.ok()) {
		S5LOG_ERROR("Failed GET key:%s, %s", INODE_SEED_KEY, s.ToString().c_str());
	}
	else {
		fprintf(stderr, "Succeed get key:%s\n", INODE_SEED_KEY);
	}

	ctx.db->Flush(FlushOptions(), cfs );

	_c.cancel_all();


	return 0;
}

void __PfAof_init();
static void vn_debug_run(const char* db_path)
{

	Options options;
	ConfigOptions config_options;
	static std::shared_ptr<ROCKSDB_NAMESPACE::Env> env_guard;
	static ROCKSDB_NAMESPACE::Env* FLAGS_env = ROCKSDB_NAMESPACE::Env::Default();
	ViveFsContext* ctx = new ViveFsContext();
	Cleaner _c;
	_c.push_back([ctx]() {delete ctx; });
	Status s =
		Env::CreateFromUri(config_options, "", "pfaof", &FLAGS_env, &env_guard);
	if (!s.ok()) {
		S5LOG_ERROR("Failed creating env: %s\n", s.ToString().c_str());
		return ;
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
	//data_cf_opt.merge_operator.reset(new ViveDataMergeOperator());
	column_families.push_back(ColumnFamilyDescriptor(
		"data_cf", data_cf_opt));
	std::vector<ColumnFamilyHandle*> handles;
	ctx->db_path = db_path;
	options.manual_wal_flush = true;
	s = TransactionDB::Open(options, tx_opt, ctx->db_path.c_str(), column_families, &handles, &ctx->db);
	if (!s.ok()) {
		S5LOG_FATAL("Failed open db:%s, %s", ctx->db_path.c_str(), s.ToString().c_str());
	} else {
		fprintf(stderr, "Open db OK\n");
	}
	ctx->default_cf = handles[0];
	ctx->meta_cf = handles[1];
	ctx->data_cf = handles[2];

	ctx->meta_opt.sync = true;


	const char* k = INODE_SEED_KEY;
	PinnableSlice v;
	s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, INODE_SEED_KEY, &v);
	if (!s.ok()) {
		S5LOG_ERROR("Failed GET key:%s, %s",k, s.ToString().c_str());
	}
	else {
		fprintf(stderr, "Succeed get key:%s\n", k);
	}

}
void print_usage()
{
	fprintf(stderr, "format a rocksdb database as ViveFS\n "
		"Usage: mkfs.vn <db_path>\n");
}
int main(int argc, char** argv) {
	if (argc == 3 && strcmp(argv[1], "-d") == 0) {
		//if (argc != 3) {
		//	printf("To run as debug tool :mkfs.vn -d <db_path>\n");
		//	return 1;
		//}
		printf("run as debug tool, with  db:%s\n", argv[2]);
		vn_debug_run(argv[2]);
		return 0;
	}
	if(argc != 2 || strcmp(argv[1], "-h")== 0 || strcmp(argv[1], "--help") == 0){
		print_usage();
		return 1;
	}
	printf("Begin ViveFS format on db:%s\n", argv[1]);
	int rc = vn_mkfs_vivefs(argv[1]);
	if (rc)
		printf("Format failed, rc:%d\n", rc);
	else
		printf("Format successed\n");

	return rc;
}
