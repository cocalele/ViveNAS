#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "rocksdb/slice_transform.h"
#include "vn_vivenas_internal.h"

using namespace ROCKSDB_NAMESPACE;

using ROCKSDB_NAMESPACE::Options;


static void setup_db_options0(Options& options)
{
	options.IncreaseParallelism();
	options.OptimizeLevelStyleCompaction();
	// create the DB if it's not already present
	options.create_if_missing = true;
}
static void setup_data_cf_options0(ColumnFamilyOptions& options)
{
}

static void setup_meta_cf_options0(ColumnFamilyOptions& options)
{
}

void setup_db_options1(Options &options)
{	
	options.env->SetBackgroundThreads(4);
	options.compaction_style = kCompactionStyleLevel;
	options.write_buffer_size = 67108864; // 64MB, size of a single memtable. Once memtable exceeds this size, it is marked immutable and a new one is created.
	options.max_write_buffer_number = 3;
	options.target_file_size_base = 67108864; // 64MB
	options.max_background_compactions = 4;
	options.level0_file_num_compaction_trigger = 8;
	options.level0_slowdown_writes_trigger = 17;
	options.level0_stop_writes_trigger = 24;
	options.num_levels = 4;
	options.max_bytes_for_level_base = 536870912; // 512MB
	options.max_bytes_for_level_multiplier = 10;
	options.target_file_size_multiplier = 2;

	// Optimize RocksDB. This is the easiest way to get RocksDB to perform well
	options.IncreaseParallelism();
	options.OptimizeLevelStyleCompaction();
	// create the DB if it's not already present
	options.create_if_missing = true;

	

}

class ExtPrefixExtrator : public rocksdb::SliceTransform
{
public:
	ExtPrefixExtrator(){}
	ExtPrefixExtrator(ExtPrefixExtrator*) {}
	virtual const char* Name() const override
	{
		return "VNExtTransform";
	}
	virtual Slice Transform(const Slice& key) const
	{
		vn_extent_key *ext_key = (vn_extent_key * )key.data();
		return Slice((char*)&ext_key->inode_no, sizeof(ext_key->inode_no));
	}
	virtual bool InDomain(const Slice& key) const
	{
		return true;
	}
};

//some options are per column family, set it to cf
void setup_data_cf_options1(ColumnFamilyOptions& options)
{
	//setup_db_options(options.);
	options.OptimizeLevelStyleCompaction();

	options.write_buffer_size = 512 << 20;
	options.max_write_buffer_number = 5;
	options.min_write_buffer_number_to_merge = 1;

	options.target_file_size_base = 512<<20;

	rocksdb::BlockBasedTableOptions table_options;

	table_options.block_size = 256 << 10;
	table_options.block_align = true;
	table_options.block_cache = NewLRUCache(1LL * 1024 * 1024 * 1024);
	//table_options.block_cache->SetCapacity(512<<20);
	table_options.index_type = rocksdb::BlockBasedTableOptions::kHashSearch;
	
	options.table_factory.reset(NewBlockBasedTableFactory(table_options));
	options.prefix_extractor = std::make_shared< ExtPrefixExtrator>(new ExtPrefixExtrator);

	options.max_bytes_for_level_multiplier = 10;
	options.target_file_size_multiplier = 2;

}

//some options are per column family, set it to cf
void setup_meta_cf_options1(ColumnFamilyOptions& options)
{
	//setup_db_options(options);

	options.OptimizeLevelStyleCompaction();
	options.max_write_buffer_number = 5;
	options.min_write_buffer_number_to_merge = 1;

	options.target_file_size_base = 64 << 20;

	rocksdb::BlockBasedTableOptions table_options;

	table_options.block_size = 4 << 20;
	table_options.block_align = true;
	options.table_factory.reset(NewBlockBasedTableFactory(table_options));

}



static void setup_db_options2(Options& options)
{
	setup_db_options0(options);
	options.max_background_jobs = 16; //same as default value in IncreaseParallelism
	options.env->SetBackgroundThreads(16, Env::LOW);
	options.env->SetBackgroundThreads(4, Env::HIGH);

}
static void setup_data_cf_options2(ColumnFamilyOptions& options)
{
	setup_data_cf_options0(options);
}

static void setup_meta_cf_options2(ColumnFamilyOptions& options)
{
	setup_meta_cf_options0(options);
}

#define OPTION_SETUP 1
//#define DB_FUNC(x) DB_FUNC2(setup_db_options, x)
#define OPT_FUNC(f, x) OPT_FUNC2(f, x)
#define OPT_FUNC2(f, x) f ## x
void setup_db_options(Options& options)
{
	//DB_FUNC( OPTION_SETUP)(options);
	OPT_FUNC(setup_db_options, OPTION_SETUP)(options);
}
void setup_data_cf_options(ColumnFamilyOptions& options)
{
	OPT_FUNC(setup_data_cf_options, OPTION_SETUP)(options);
}

void setup_meta_cf_options(ColumnFamilyOptions& options)
{
	OPT_FUNC(setup_meta_cf_options, OPTION_SETUP)(options);
}