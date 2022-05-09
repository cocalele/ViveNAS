#include <rocksdb/file_system.h>

namespace ROCKSDB_NAMESPACE {

// Returns a `FileSystem` that hashes file contents when naming files, thus
// deduping them. RocksDB however expects files to be identified based on a
// monotonically increasing counter, so a mapping of RocksDB's name to content
// hash is needed. This mapping is stored in a separate RocksDB instance.
std::unique_ptr<ROCKSDB_NAMESPACE::FileSystem> NewPfAofFileSystem();

}  // namespace ROCKSDB_NAMESPACE
