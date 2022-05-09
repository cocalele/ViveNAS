#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/convenience.h"
#include <rocksdb/utilities/transaction.h>
#include <fcntl.h>

#include "vivenas.h"
#include <nlohmann/json.hpp>
#include <string>
#include "pf_utils.h"
#include "vivenas.h"
#include "vivenas_internal.h"

using namespace std;
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
using ROCKSDB_NAMESPACE::Slice;

using nlohmann::json;
using namespace std;
#define VIVEFS_MAGIC_STR "vivefs_0"
#define VIVEFS_VER 0x00010000

//void to_json(json& j, const ViveInode& n)
//{
//	j = json{ 
//		{"i_mode", n.i_mode},
//		{"i_uid", n.i_uid},
//		{"i_size", n.i_size},
//		{"i_atime", n.i_atime},
//		{"i_ctime", n.i_ctime},
//		{"i_mtime", n.i_mtime},
//		{"i_dtime", n.i_dtime},
//		{"i_gid", n.i_gid},
//		{"i_links_count", n.i_links_count},
//		{"i_flags", n.i_flags},
//
//	};
//}
int vn_create_file(ViveFsContext* ctx, int64_t parent_inode_no, const char* file_name, int16_t mode)
{
	ViveInode inode = { 0 };
	inode.i_links_count = 1;
	inode.i_mode = mode;
	char buf[VIVE_INODE_SIZE];
	assert(sizeof(struct ViveInode) <= VIVE_INODE_SIZE);
	memset(buf, 0, sizeof(buf));
	memcpy(buf, &inode, sizeof(struct ViveInode));
	Slice file_key = format_string("%ld_%s", parent_inode_no, file_name);
	inode.i_no = ctx->generate_inode_no();
	
	Transaction* tx = ctx->db->BeginTransaction(ctx->meta_opt);
	DeferCall _1([tx]() {delete tx; });
	Cleaner _c;
	_c.push_back([tx]() {tx->Rollback(); });
	PinnableSlice old_inode;
	Status s = tx->GetForUpdate(ctx->read_opt, ctx->meta_cf, file_key, &old_inode);
	if(!s.ok()){
		S5LOG_ERROR("Failed lock key:%s, for:%s", file_key.data(), s.ToString().c_str());
		return -EIO;
	}
	s = tx->Put(ctx->meta_cf, file_key, Slice(buf, VIVE_INODE_SIZE));
	if (!s.ok()) {
		S5LOG_ERROR("Failed put inode:%s, for:%s", file_key.data(), s.ToString().c_str());
		return -EIO;
	}
	s = tx->Put(ctx->meta_cf, INODE_SEED_KEY, Slice((char*)&ctx->inode_seed, sizeof(ctx->inode_seed)));
	if (!s.ok()) {
		S5LOG_ERROR("Failed put seed:%s, for:%s", file_key.data(), s.ToString().c_str());
		return -EIO;
	}
	
	s = tx->Commit();
	if (!s.ok()) {
		S5LOG_ERROR("Failed create file:%s on committing, for:%s", file_key.data(), s.ToString().c_str());
		return -EIO;
	}
	_c.cancel_all();
	return 0;
}


struct ViveFile* vn_open_file(ViveFsContext* ctx, int64_t parent_inode_no, const char* file_name, int32_t flags, int16_t mode)
{
	int rc = 0;
	PinnableSlice inode_buf;
	Slice file_key = format_string("%ld_%s", parent_inode_no, file_name);
	Status s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, file_key, &inode_buf);
	if(s.IsNotFound()){
		S5LOG_INFO("Open file %s not exists", file_key.data());
		if(flags & O_CREAT){
			S5LOG_INFO("Creating file %s ...", file_key.data());
			rc = vn_create_file(ctx, parent_inode_no, file_name, mode);
			if (rc < 0) {
				return NULL;
			}
		}
		return NULL;
	}
	

	ViveInode* inode = (ViveInode*)inode_buf.data();

	if(flags & O_TRUNC) {
		Transaction* tx = ctx->db->BeginTransaction(ctx->meta_opt);
		DeferCall _1([tx]() {delete tx; });
		Cleaner _c;
		_c.push_back([tx]() {tx->Rollback(); });

		//TODO: is PinnableSlice used correctly?
		PinnableSlice inode_buf;
		Status s = tx->GetForUpdate(ctx->read_opt, ctx->meta_cf, file_key, &inode_buf);
		if (!s.ok()) {
			S5LOG_ERROR("Failed to open file:%s, for:%s", file_key.data(), s.ToString().c_str());
			return NULL;
		}
		int64_t ext_cnt = (inode->i_size + inode->i_extent_size - 1) / inode->i_extent_size;
		for(int64_t i = 0;i<ext_cnt;i++){
			pfs_extent_key ext_k = { {.extent_index = i, .inode_no = inode->i_no} };
			tx->Delete(ctx->data_cf, &ext_key);
		}
		inode->i_size = 0;
		tx->Put(ctx->meta_cf, file_key, Slice((char*)inode, VIVE_INODE_SIZE));
		s = tx->Commit();
		if (!s.ok()) {
			S5LOG_ERROR("Failed to commit deleteing op, for:%s", s.ToString().c_str());
			return NULL;
		}
		_c.cancel_all();
	}
	ViveFile* f = new ViveFile;

	f->i_no = inode->i_no;
	return f;
}

ssize_t vn_write(ViveFsContext* ctx, struct ViveFile* file, const char * in_buf, size_t len, off_t offset )
{
	int64_t start_ext = offset / file->inode->i_extent_size;
	int64_t end_ext = (offset + len) / file->inode->i_extent_size;
	void* buf = malloc(file->inode->i_extent_size + EXT_HEAD_SIZE);
	if (buf == NULL) {
		S5LOG_ERROR("Failed alloc memory");
		return -ENOMEM;
	}
	DeferCall _1([buf]() {free(buf); });
	struct pfs_extent_head* head = (struct pfs_extent_head*)buf;

	
	int64_t buf_offset = 0;

	for (int64_t index = start_ext; index <= end_ext; index++) {

		//string ext_key = format_string("%ld_%ld", file->i_no, index);
		pfs_extent_key ext_key = { {.extent_index = index, .inode_no = file->i_no} };

		int64_t start_off = (offset + buf_offset) % file->inode->i_extent_size; //offset in extent
		int64_t segment_len = min(len - buf_offset, file->inode->i_extent_size - start_off);
		Status s;
		*head = { 0 };
		memcpy(buf + EXT_HEAD_SIZE, in_buf + buf_offset, segment_len);
		//TODO: implement a vector slice so we can combine extent_head and data together without memcpy
		Slice setment_data(buf, segment_len + EXT_HEAD_SIZE);
		if(segment_len != file->inode->i_extent_size){
			head->merge_off = start_off;
			s = ctx->db->Merge(ctx->data_opt, ctx->data_cf, Slice(&ext_key, sizeof(ext_key)), &setment_data);
		} else {
			head->data_bmp = PFS_FULL_EXTENT_BMP;
			s = ctx->db->Put(ctx->data_opt, ctx->data_cf, Slice(&ext_key, sizeof(ext_key)), &setment_data);
		}
		if(!s.ok()){
			S5LOG_ERROR("Failed write on key:%ld_%ld len:%ld, for:%s", ext_key.inode_no, ext_key.extent_index, segment_len, s.ToString().c_str());
		}
		buf_offset += segment_len;
	}
	
}

ssize_t vn_read(ViveFsContext* ctx, struct ViveFile* file, char* out_buf, size_t len, off_t offset)
{
	int64_t start_ext = offset / file->inode->i_extent_size;
	int64_t end_ext = (offset + len)/ file->inode->i_extent_size;

	if (offset + len > file->inode->i_size)
		len = file->inode->i_size - offset;
	int64_t buf_offset = 0;


	for (int64_t index = start_ext; index <= end_ext; index++) {

		pfs_extent_key ext_key = { {.extent_index = index, .inode_no = file->i_no} };
		int64_t start_off = (offset + buf_offset) % file->inode->i_extent_size; //offset in extent
		int64_t segment_len = min(len - buf_offset, file->inode->i_extent_size - start_off);
		Status s;
		PinnableSlice segment_data;

		//TODO: Is it better to use MultiGet or Iterator? 
		s = ctx->db->Get(ctx->data_opt, ctx->data_cf, Slice(&ext_key, sizeof(ext_key)), &segment_data);
		if (!s.ok()) {
			S5LOG_ERROR("Failed write on key:%ld_%ld len:%ld, for:%s", ext_key.inode_no, ext_key.extent_index, segment_len, s.ToString().c_str());
		}
		if (segment_data.len() > 0)
			memcpy(out_buf + buf_offset, segment_data.data() + EXT_HEAD_SIZE, segment_len);
		else
			memset(out_buf + buf_offset, 0, segment_len);
		buf_offset += segment_len;
	}

	return len;
}

int vn_delete(ViveFsContext* ctx, struct ViveFile* file)
{
	Slice file_key = format_string("%ld_%s", file->parent_inode_no, file->file_name.c_str());

	Transaction* tx = ctx->db->BeginTransaction(ctx->meta_opt);
	DeferCall _1([tx]() {delete tx; });
	Cleaner _c;
	_c.push_back([tx]() {tx->Rollback(); });


	int64_t ext_cnt = (inode->i_size + inode->i_extent_size - 1) / inode->i_extent_size;
	for (int64_t i = 0; i < ext_cnt; i++) {
		pfs_extent_key ext_k = { {.extent_index = i, .inode_no = file->inode->i_no} };
		tx->Delete(ctx->default_cf, &ext_key);
	}
	tx->Delete(ctx->meta_cf, file_key);
	s = tx->Commit();
	if (!s.ok()) {
		S5LOG_ERROR("Failed delete file:%s on committing, for:%s", file_key.data(), s.ToString().c_str());
		return -EIO;
	}
	_c.cancel_all();
	return 0;
}

