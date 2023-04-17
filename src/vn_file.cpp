#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/convenience.h"
#include <rocksdb/utilities/transaction.h>
#include <fcntl.h>
#include <algorithm>

#include "vn_vivenas.h"
#include <nlohmann/json.hpp>
#include <string>
#include "pf_utils.h"
#include "vn_vivenas.h"
#include "vn_vivenas_internal.h"

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
static __always_inline Status _vn_persist_inode(ViveFsContext* ctx, Transaction* tx, ViveInode* inode);
static __always_inline struct ViveInode* vn_alloc_inode()
{
	struct ViveInode* n = (struct ViveInode*)calloc(1, sizeof(struct ViveInode));
	n->ref_cnt=1;
	return n;
}

static struct ViveInode* deserialize_inode(const char* buf)
{
	struct ViveInode* n = (struct ViveInode*)malloc(sizeof(struct ViveInode));
	if(n == NULL){
		S5LOG_ERROR("Failed to alloc inode memory");
		return NULL;
	}
	memcpy(n, buf, sizeof(*n));
	n->ref_cnt = 1;
	return n;
}
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


//caller must ensure target file/dir not exists
int64_t vn_create_file(ViveFsContext* ctx, int64_t parent_inode_no, const char* file_name, int16_t mode, int16_t uid, int16_t gid, ViveInode** inode_out)
{
	ViveInode *inode = vn_alloc_inode();
	inode->i_links_count = 1;
	inode->i_mode = mode;
	inode->i_no = ctx->generate_inode_no();
	inode->i_extent_size = VIVEFS_EXTENT_SIZE;
	inode->i_atime = inode->i_ctime = inode->i_mtime = time(NULL);
	inode->i_uid = uid;
	inode->i_gid = gid;
	assert(sizeof(struct ViveInode) == VIVEFS_INODE_SIZE);
	string s1 = format_string("%ld_%s", parent_inode_no, file_name);
	Slice file_key = s1;
	
	Transaction* tx = ctx->db->BeginTransaction(ctx->meta_opt);
	DeferCall _1([tx]() {delete tx; });
	Cleaner _c;
	_c.push_back([tx]() {tx->Rollback(); });
	PinnableSlice old_inode;


	//PinnableSlice parent_inode_no_buf;
	//Status s = tx->GetForUpdate(ctx->read_opt, ctx->meta_cf,
	//	Slice((char*)&parent_inode_no, sizeof(parent_inode_no)), &parent_inode_no_buf);
	//if (!s.ok()) {
	//	S5LOG_ERROR("Failed lock parent inode:%ld, for:%s", parent_inode_no, s.ToString().c_str());
	//	return vn_inode_no_t(-EIO);
	//}

	Status s = tx->GetForUpdate(ctx->read_opt, ctx->meta_cf, file_key, &old_inode);
	if(s.ok()){
		S5LOG_ERROR("File already exists, key:%s", file_key.data());
		return  -EEXIST;
	}
	int64_t ino = inode->i_no;

	Slice inode_no_as_slice((const char*)&ino, sizeof(ino));
	s = tx->Put(ctx->meta_cf, file_key, inode_no_as_slice);
	if (!s.ok()) {
		S5LOG_ERROR("Failed put file inode:%s, for:%s", file_key.data(), s.ToString().c_str());
		return  -EIO;
	}
	s = _vn_persist_inode(ctx, tx, inode);
	if (!s.ok()) {
		S5LOG_ERROR("Failed put inode:%s, for:%s", file_key.data(), s.ToString().c_str());
		return  -EIO;
	}
	s = tx->Put(ctx->meta_cf, INODE_SEED_KEY, Slice((char*)&ctx->inode_seed, sizeof(ctx->inode_seed)));
	if (!s.ok()) {
		S5LOG_ERROR("Failed put seed:%s, for:%s", file_key.data(), s.ToString().c_str());
		return  -EIO;
	}
	
	s = tx->Commit();
	if (!s.ok()) {
		S5LOG_ERROR("Failed create file:%s on committing, for:%s", file_key.data(), s.ToString().c_str());
		return -EIO;
	}

	_c.cancel_all();
	//S5LOG_INFO("Create file:%ld_%s, new ino:%ld, mode:00%o (octal)", parent_inode_no, file_name, ino, inode->i_mode);
	if (inode_out != NULL)
		*inode_out = inode;
	else
		vn_dec_inode_ref(inode);
	return ino;
}


int update_file_size(ViveFsContext* ctx, Transaction* tx, ViveInode* inode, size_t new_size)
{
	assert(0);
	return 0;
}
static __always_inline Status _vn_persist_inode(ViveFsContext* ctx, Transaction* tx, ViveInode* inode)
{
	//S5LOG_DEBUG("persist inode inode.ino:%ld", inode->i_no);
	Status s = tx->Put(ctx->meta_cf, Slice((char*)&inode->i_no, sizeof(inode->i_no)), Slice((char*)inode, VIVEFS_INODE_SIZE));

	return s;
}

struct ViveFile* vn_open_file_by_inode(ViveFsContext* ctx, struct ViveInode* inode, int32_t flags, int16_t mode)
{
	Status s;
	//ViveInode* inode = NULL;
	//PinnableSlice inode_buf;
	//vn_inode_no_t ino(ino_);
	//if(ino.to_int() == VN_ROOT_INO){
	//	inode = &ctx->root_inode;
	//} else {
	//	s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, Slice((char*)&ino, sizeof(ino)), &inode_buf);
	//	if (s.IsNotFound()) {
	//		S5LOG_ERROR("Internal error, open file ino:%d failed, inode not exists", ino.to_int());
	//		return NULL;
	//	}
	//	inode = (ViveInode*)inode_buf.data();
	//}

	vn_inode_no_t ino(inode->i_no);

	if (flags & O_TRUNC) {
		S5LOG_DEBUG("truncate file ino:%d", ino);
		Transaction* tx = ctx->db->BeginTransaction(ctx->meta_opt);
		DeferCall _1([tx]() {delete tx; });
		Cleaner _c;
		_c.push_back([tx]() {tx->Rollback(); });

		//TODO: is PinnableSlice used correctly?
		PinnableSlice inode_buf;
		Status s = tx->GetForUpdate(ctx->read_opt, ctx->meta_cf, Slice((char*)&ino, sizeof(ino)), &inode_buf);
		if (!s.ok()) {
			S5LOG_ERROR("Failed to open file:%ld, for:%s", ino.to_int(), s.ToString().c_str());
			return NULL;
		}
		int64_t ext_cnt = (inode->i_size + inode->i_extent_size - 1) / inode->i_extent_size;

		for (int64_t i = 0; i < ext_cnt; i++) {
			pfs_extent_key ext_k = { {{extent_index: (__le64)i, inode_no : (__le64)inode->i_no}} };

			tx->Delete(ctx->data_cf, Slice((const char*)&ext_k, sizeof(ext_k)));
		}
		inode->i_size = 0;
		s = _vn_persist_inode(ctx, tx, inode);
		if (!s.ok()) {
			S5LOG_ERROR("Failed to persist inode:%d, for:%s", inode->i_no, s.ToString().c_str());
			return NULL;
		}

		s = tx->Commit();
		if (!s.ok()) {
			S5LOG_ERROR("Failed to commit deleteing op, for:%s", s.ToString().c_str());
			return NULL;
		}
		_c.cancel_all();
	}
	ViveFile* f = new ViveFile;

	f->i_no = inode->i_no;
	vn_add_inode_ref(inode);
	f->inode = inode;
	if(flags & O_NOATIME)
		f->noatime=1;
	return f;
}
struct ViveFile* vn_open_file(ViveFsContext* ctx, int64_t parent_inode_no, const char* file_name, int32_t flags, int16_t mode)
{
	PinnableSlice inode_no_buf;
	PinnableSlice inode_buf;
	int64_t ino;

	string s1 = format_string("%ld_%s", parent_inode_no, file_name);
	Slice file_key = s1;
	Status s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, file_key, &inode_no_buf);
	if(s.IsNotFound()){
		//S5LOG_INFO("Open file %s not exists", file_key.data());
		if(flags & O_CREAT){
			S5LOG_INFO("Creating file %s, mode:00%o(Octal) ...", file_key.data(), mode);
			S5LOG_WARN("Creating file %s, with uid 0 and gid 0", file_key.data());
			ino = vn_create_file(ctx, parent_inode_no, file_name, mode, 0, 0, NULL);
			if (ino < 0) {
				return NULL;
			}
		} else {
			return NULL;
		}
	} else {
		memcpy(&ino, inode_no_buf.data(), sizeof(ino));
	}
	
	s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, Slice((char*)&ino, sizeof(ino)), &inode_buf);
	if (s.IsNotFound()) {
		S5LOG_ERROR("Internal error, open file ino:%d failed, inode not exists", ino);
		return NULL;
	}
	ViveInode* inode = deserialize_inode(inode_buf.data());
	if(inode == NULL)
		return NULL;
	struct ViveFile* f = vn_open_file_by_inode(ctx, inode, flags, mode);
	vn_dec_inode_ref(inode);
	return f;
}

size_t vn_write(struct ViveFsContext* ctx, struct ViveFile* file, const char * in_buf, size_t len, off_t offset )
{
	int64_t start_ext = offset / file->inode->i_extent_size;
	int64_t end_ext = (offset + len + file->inode->i_extent_size - 1) / file->inode->i_extent_size;
	void* buf = malloc(file->inode->i_extent_size + PFS_EXTENT_HEAD_SIZE);
	if (buf == NULL) {
		S5LOG_ERROR("Failed alloc memory");
		return -ENOMEM;
	}
	S5LOG_DEBUG("call vn_write, len:%ld off:%ld", len, offset);
	DeferCall _1([buf]() {free(buf); });
	struct pfs_extent_head* head = (struct pfs_extent_head*)buf;
	Transaction* tx = ctx->db->BeginTransaction(ctx->data_opt);
	DeferCall _2([tx]() {delete tx; });
	Cleaner _c;
	Status s;
	_c.push_back([tx]() {tx->Rollback(); });

	int64_t buf_offset = 0;

	for (int64_t index = start_ext; index < end_ext; index++) {

		//string ext_key = format_string("%ld_%ld", file->i_no, index);
		pfs_extent_key ext_key = { {{extent_index: (__le64)index, inode_no : (__le64)file->i_no}} };

		int64_t start_off = (offset + buf_offset) % file->inode->i_extent_size; //offset in extent
		size_t segment_len = std::min(len - buf_offset, (size_t)file->inode->i_extent_size - start_off);
		
		*head = { 0 };
		memcpy((char*)buf + PFS_EXTENT_HEAD_SIZE, in_buf + buf_offset, segment_len);
		//TODO: implement a vector slice so we can combine extent_head and data together without memcpy
		Slice segment_data((char*)buf, segment_len + PFS_EXTENT_HEAD_SIZE);
		if(segment_len != file->inode->i_extent_size){
			head->merge_off = (uint16_t)start_off;
			s = tx->Merge(ctx->data_cf, Slice((const char*)&ext_key, sizeof(ext_key)), segment_data);
		} else {
			head->data_bmp = (uint16_t)start_off; 
			s = tx->Put(ctx->data_cf, Slice((const char*)&ext_key, sizeof(ext_key)), segment_data);
		}
		if(!s.ok()){
			S5LOG_ERROR("Failed write on key:%ld_%ld len:%ld, for:%s", ext_key.inode_no, ext_key.extent_index, segment_len, s.ToString().c_str());
		}
		buf_offset += segment_len;
	}
	
	file->inode->i_mtime = time(NULL);
	file->dirty = 1;
	
	if (offset + len > file->inode->i_size) {
		file->inode->i_size = offset + len;
		s = _vn_persist_inode(ctx, tx, file->inode);
		if (!s.ok()) {
			S5LOG_ERROR("Failed to persist inode, for:%s", s.ToString().c_str());
			return -EIO;
		}
		file->dirty = 0;
	}
	s = tx->Commit();
	if (!s.ok()) {
		S5LOG_ERROR("Commit failed, for:%s", s.ToString().c_str());
		return -EIO;
	}
	_c.cancel_all();
	return len;
}

size_t vn_writev(struct ViveFsContext* ctx, struct ViveFile* file, struct iovec in_iov[], int iov_cnt, off_t offset)
{
	size_t len = 0;
	for (int i = 0; i < iov_cnt; i++) {
		len += in_iov[i].iov_len;
	}
	int64_t start_ext = offset / file->inode->i_extent_size;
	int64_t end_ext = (offset + len + file->inode->i_extent_size - 1) / file->inode->i_extent_size;
	//S5LOG_DEBUG("call vn_writev, iov_cnt:%ld off:%ld", iov_cnt, offset);
	void* buf = malloc(file->inode->i_extent_size + PFS_EXTENT_HEAD_SIZE);
	if (buf == NULL) {
		S5LOG_ERROR("Failed alloc memory");
		return -1;
	}
	DeferCall _1([buf]() {free(buf); });
	struct pfs_extent_head* head = (struct pfs_extent_head*)buf;
	Transaction* tx = ctx->db->BeginTransaction(ctx->data_opt);
	DeferCall _2([tx]() {delete tx; });
	Cleaner _c;
	_c.push_back([tx]() {tx->Rollback(); });


	int64_t buf_offset = 0;
	int iov_idx = 0;
	int64_t in_iov_off = 0;

	for (int64_t index = start_ext; index < end_ext; index++) {

		//string ext_key = format_string("%ld_%ld", file->i_no, index);
		pfs_extent_key ext_key = { {{extent_index: (__le64)index, inode_no : (__le64)file->i_no}} };

		int64_t start_off = (offset + buf_offset) % file->inode->i_extent_size; //offset in extent
		size_t segment_len = min(len - buf_offset, (size_t)file->inode->i_extent_size - start_off);
		Status s;
		*head = { 0 };

		size_t off = 0;
		size_t data_remain = segment_len;
		assert(data_remain > 0);
		while (data_remain > 0) {
			size_t iov_remain = in_iov[iov_idx].iov_len - in_iov_off;
			size_t to_copy = min(iov_remain, data_remain);
			memcpy((char*)buf + PFS_EXTENT_HEAD_SIZE + off, (char*)in_iov[iov_idx].iov_base + in_iov_off, to_copy);
			off += to_copy;
			data_remain -= to_copy;
			in_iov_off += to_copy;
			if (in_iov_off == in_iov[iov_idx].iov_len) {
				iov_idx++;
				in_iov_off = 0;
			}
		}


		//TODO: implement a vector slice so we can combine extent_head and data together without memcpy
		Slice segment_data((const char*)buf, segment_len + PFS_EXTENT_HEAD_SIZE);
		if (segment_len != file->inode->i_extent_size) {
			head->merge_off = (uint16_t)start_off;
			//S5LOG_DEBUG("Merge data on key:%s %ld bytes", ext_key.to_string(), segment_data.size());
			s = tx->Merge(ctx->data_cf, Slice((const char*)&ext_key, sizeof(ext_key)), segment_data);
			//s = tx->Put(ctx->data_cf, Slice((const char*)&ext_key, sizeof(ext_key)), segment_data);
		}
		else {
			head->data_bmp = (uint16_t)start_off;;
			//S5LOG_DEBUG("Put data on key:%s %ld bytes", ext_key.to_string(), segment_data.size());
			s = tx->Put(ctx->data_cf, Slice((const char*)&ext_key, sizeof(ext_key)), segment_data);
		}
		if (!s.ok()) {
			S5LOG_ERROR("Failed write on key:%ld_%ld len:%ld, for:%s", ext_key.inode_no, ext_key.extent_index, segment_len, s.ToString().c_str());
		}
		buf_offset += segment_len;
	}
	file->inode->i_mtime = time(NULL);
	file->dirty = 1;

	if(offset + len > file->inode->i_size){
		file->inode->i_size = offset + len;
		Status s = _vn_persist_inode(ctx, tx, file->inode);
		if (!s.ok()) {
			S5LOG_ERROR("Failed to persist inode, for:%s", s.ToString().c_str());
			return -EIO;
		}
		file->dirty = 0;
	}
	Status s = tx->Commit();
	if (!s.ok()) {
		S5LOG_ERROR("Commit failed in vn_writev, for:%s", s.ToString().c_str());
		return -EIO;
	}

	_c.cancel_all();
	return len;
}
size_t vn_read(struct ViveFsContext* ctx, struct ViveFile* file, char* out_buf, size_t len, off_t offset)
{
	int64_t start_ext = offset / file->inode->i_extent_size;
	int64_t end_ext = (offset + len + file->inode->i_extent_size - 1)/ file->inode->i_extent_size;

	if (offset + len > file->inode->i_size)
		len = file->inode->i_size - offset;
	int64_t buf_offset = 0;


	for (int64_t index = start_ext; index < end_ext; index++) {

		pfs_extent_key ext_key = { {{extent_index : (__le64)index, inode_no : (__le64)file->i_no}} };
		int64_t start_off = (offset + buf_offset) % file->inode->i_extent_size; //offset in extent
		size_t segment_len = min(len - buf_offset, (size_t)file->inode->i_extent_size - start_off);
		Status s;
		PinnableSlice segment_data;

		//TODO: Is it better to use MultiGet or Iterator? 
		s = ctx->db->Get(ctx->read_opt, ctx->data_cf, Slice((const char*)&ext_key, sizeof(ext_key)), &segment_data);
		if (!s.ok()) {
			S5LOG_ERROR("Failed read on file:%s key:%s len:%ld, for:%s", file->file_name.c_str(), ext_key.to_string(), segment_len, s.ToString().c_str());
		}
		if (segment_data.size() > 0)
			memcpy(out_buf + buf_offset, segment_data.data() + PFS_EXTENT_HEAD_SIZE, segment_len);
		else
			memset(out_buf + buf_offset, 0, segment_len);
		buf_offset += segment_len;
	}

	if(!file->noatime){
		file->inode->i_atime = time(NULL);
		file->dirty = 1;
	}
	return len;
}
size_t vn_readv(struct ViveFsContext* ctx, struct ViveFile* file, struct iovec out_iov[], int iov_cnt, off_t offset)
{
	size_t len = 0;
	for(int i=0;i<iov_cnt;i++){
		len += out_iov[i].iov_len;
	}
	int64_t start_ext = offset / file->inode->i_extent_size;
	int64_t end_ext = (offset + len + file->inode->i_extent_size - 1) / file->inode->i_extent_size;
	//S5LOG_DEBUG("call vn_readv, iov_cnt:%d, off:%d", iov_cnt, offset);
	if (offset + len > file->inode->i_size)
		len = file->inode->i_size - offset;
	int64_t buf_offset = 0;

	int iov_idx = 0;
	int64_t in_iov_off = 0;

	for (int64_t index = start_ext; index < end_ext; index++) {

		pfs_extent_key ext_key = { { {extent_index: (__le64)index, inode_no : (__le64)file->i_no} } };
		int64_t start_off = (offset + buf_offset) % file->inode->i_extent_size; //offset in extent
		size_t segment_len = min(len - buf_offset, (size_t)file->inode->i_extent_size - start_off);
		Status s;
		PinnableSlice segment_data;

		//TODO: Is it better to use MultiGet or Iterator? 
		s = ctx->db->Get(ctx->read_opt, ctx->data_cf, Slice((const char*)&ext_key, sizeof(ext_key)), &segment_data);
		if (!s.ok()) {
			S5LOG_ERROR("Failed read on key:%s len:%ld, for:%s", ext_key.to_string(), segment_len, s.ToString().c_str());
		}

		size_t off = 0;
		size_t data_remain = segment_len;
		if (segment_data.size() > 0) {
			//assert(segment_len == file->inode->i_extent_size);
			while (data_remain > 0) {
				size_t iov_remain = out_iov[iov_idx].iov_len - in_iov_off;
				size_t to_copy = min(iov_remain, data_remain);
				memcpy((char*)out_iov[iov_idx].iov_base + in_iov_off, segment_data.data() + PFS_EXTENT_HEAD_SIZE + off, to_copy);
				off += to_copy;
				data_remain -= to_copy;
				in_iov_off += to_copy;
				if (in_iov_off == out_iov[iov_idx].iov_len) {
					iov_idx++;
					in_iov_off = 0;
				}
			}
			
		}
		else {
			while (data_remain > 0) {
				size_t iov_remain = out_iov[iov_idx].iov_len - in_iov_off;
				size_t to_copy = min(iov_remain, data_remain);
				memset((char*)out_iov[iov_idx].iov_base + in_iov_off, 0, to_copy);
				off += to_copy;
				data_remain -= to_copy;
				in_iov_off += to_copy;
				if (in_iov_off == out_iov[iov_idx].iov_len) {
					iov_idx++;
					in_iov_off = 0;
				}
			}
		}
		
	
		buf_offset += segment_len;
	}
	//S5LOG_DEBUG("onread noatime:%d", file->noatime);

	if (!file->noatime) {
		file->inode->i_atime = time(NULL);
		file->dirty = 1;
	}

	return len;
}

int vn_unlink(ViveFsContext* ctx, int64_t parent_ino, const char* fname)
{
	S5LOG_DEBUG("To unlink file:%ld_%s ", parent_ino, fname);
	struct ViveInode* inode;
	int64_t ino = vn_lookup_inode_no(ctx, parent_ino, fname, &inode);
	if (ino < 0)
		return -ENOENT;

	DeferCall _0([inode]() {vn_dec_inode_ref(inode); });

	Transaction* tx = ctx->db->BeginTransaction(ctx->meta_opt);
	DeferCall _1([tx]() {delete tx; });
	Cleaner _c;
	_c.push_back([tx]() {tx->Rollback(); });
	if( --inode->i_links_count == 0){
		string s1 = format_string("%ld_%s", parent_ino, fname);
		Slice file_key = s1;

		int64_t ext_cnt = (inode->i_size + inode->i_extent_size - 1) / inode->i_extent_size;
		for (int64_t i = 0; i < ext_cnt; i++) {
			pfs_extent_key ext_k = { {{extent_index: (__le64)i, inode_no : (__le64)inode->i_no}} };
			tx->Delete(ctx->default_cf, Slice((const char*)&ext_k, sizeof(ext_k)));
		}
		tx->Delete(ctx->meta_cf, file_key);
	} else {
		Status s = _vn_persist_inode(ctx, tx, inode);
		if (!s.ok()) {
			S5LOG_ERROR("Failed to persist inode, for:%s", s.ToString().c_str());
			return -EIO;
		}
	}
	Status s = tx->Commit();
	if (!s.ok()) {
		S5LOG_ERROR("Failed unlink file:%ld_%s on committing, for:%s", parent_ino, fname, s.ToString().c_str());
		return -EIO;
	}

	_c.cancel_all();
	return 0;
}

int64_t vn_lookup_inode_no(ViveFsContext* ctx, int64_t parent_inode_no, const char* file_name, ViveInode** inode)
{
	PinnableSlice inode_buf;
	PinnableSlice inode_no_buf;
	//S5LOG_DEBUG("Lookup on parent ino:%ld file_name:%s", parent_inode_no, file_name);
	string s1 = format_string("%ld_%s", parent_inode_no, file_name);
	Slice file_key = s1;
	Status s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, file_key, &inode_no_buf);
	if (s.IsNotFound()) {
		//S5LOG_DEBUG("Lookup on parent ino:%ld file_name:%s, Not found", parent_inode_no, file_name);

		return -1;
	}
	
	if(inode != NULL) {
		s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, inode_no_buf, &inode_buf);
		if (s.IsNotFound()) {
			S5LOG_ERROR("Internal error, open file %s failed, inode lost", file_key.data());
			return  -1;
		}
		*inode = deserialize_inode(inode_buf.data());
		if(*inode == NULL)
			return -1;
	}
	int64_t ino = vn_inode_no_t(*(int64_t*)inode_no_buf.data()).to_int();
	//S5LOG_DEBUG("Lookup on parent ino:%ld file_name:%s, get ino:%ld", parent_inode_no, file_name, ino);
	return ino;
}

struct vn_inode_iterator
{
	unique_ptr<Iterator> itor;
	string prefix;
};

struct vn_inode_iterator* vn_begin_iterate_dir(ViveFsContext* ctx, int64_t parent_inode_no)
{
	struct vn_inode_iterator* it = new (struct vn_inode_iterator);
	it->prefix = format_string("%ld_", parent_inode_no);
	it->itor.reset(ctx->db->NewIterator(ctx->read_opt, ctx->meta_cf));
	it->itor->Seek(it->prefix);
	return it;
}


int /*as bool*/ vn_iterator_has_next(struct vn_inode_iterator* it)
{
	return it->itor->Valid() && it->itor->key().starts_with(it->prefix);
}

struct ViveInode* vn_next_inode(ViveFsContext* ctx, struct vn_inode_iterator* it, char* entry_name, size_t buf_len)
{
	ViveInode* inode = NULL;
	if(it->itor->Valid() && it->itor->key().starts_with(it->prefix)){
		Slice v = it->itor->value();
		int64_t ino = *(int64_t*)v.data();
		PinnableSlice inode_buf;

		//S5LOG_DEBUG("query inode with db:%p", ctx->db);
		Status s = ctx->db->Get(ctx->read_opt, ctx->meta_cf, Slice((char*)&ino, sizeof(ino)), &inode_buf);
		if (s.IsNotFound()) {
			S5LOG_ERROR("Internal error, inode lost:%ld", ino);
			return NULL;
		}
		inode = deserialize_inode(inode_buf.data());
		if(inode == NULL) return NULL;
		assert(inode->i_no == ino);

		//do {
		//	PinnableSlice inode_buf;
		//	Status s2 = ctx->db->Get(ctx->read_opt, ctx->meta_cf, Slice((char*)&ino, sizeof(ino)), &inode_buf);
		//	if (s2.IsNotFound()) {
		//		S5LOG_ERROR("Internal error, inode lost:%ld", ino);

		//	}
		//	ViveInode* inode2 = new ViveInode;

		//	memcpy(inode2, inode_buf.data(), sizeof(*inode2));
		//	assert(inode2->i_no == ino);
		//} while (0);


		Slice k = it->itor->key();
		size_t file_name_len = k.size() - it->prefix.length();
		assert(buf_len > file_name_len);
		memcpy(entry_name, k.data() + it->prefix.length(), file_name_len);
		entry_name[file_name_len] = 0;

		//S5LOG_DEBUG("iterate entry ino:%ld inode.ino:%ld name:%s", ino, inode->i_no, entry_name);
		it->itor->Next();
	}
	return inode;
}

void vn_release_iterator(ViveFsContext* ctx, struct vn_inode_iterator* it)
{
	delete it;
}

int vn_fsync(ViveFsContext* ctx, struct ViveFile* file)
{
	S5LOG_DEBUG("Sync file ino:%ld name:%s", file->i_no, file->file_name.c_str());
	Transaction* tx = ctx->db->BeginTransaction(ctx->data_opt);
	DeferCall _2([tx]() {delete tx; });
	Cleaner _c;
	_c.push_back([tx]() {tx->Rollback(); });
	if (file->dirty) {
		Status s = _vn_persist_inode(ctx, tx, file->inode);
		if(!s.ok()){
			S5LOG_ERROR("Failed to persist inode, for:%s", s.ToString().c_str());
			return -EIO;
		}
		file->dirty = 0;
	}
	Status s = tx->Commit();
	if (!s.ok()) {
		S5LOG_ERROR("Commit failed in vn_fsync for:%s", s.ToString().c_str());
		return -EIO;
	}
	s = ctx->db->FlushWAL(true);
	if(!s.ok()){
		S5LOG_ERROR("Failed sync for:%s", s.ToString().c_str());
	}
	return -EIO;
}

int vn_close_file(ViveFsContext* ctx, struct ViveFile* file)
{
	S5LOG_DEBUG("Close file ino:%ld name:%s", file->i_no, file->file_name.c_str());
	vn_fsync(ctx, file);
	delete file;
	S5LOG_DEBUG("ViveFile:%p deleted", file);
	return 0;
}

int vn_rename_file(ViveFsContext* ctx, int64_t old_dir_ino, const char* old_name, int64_t new_dir_ino, const char* new_name)
{
	S5LOG_WARN("rename not implemented");
	return 0;
}

int vn_persist_inode(struct ViveFsContext* ctx, struct ViveInode* inode)
{
	//S5LOG_DEBUG("vn_persist_inode ino:%ld", inode->i_no);
	Transaction* tx = ctx->db->BeginTransaction(ctx->data_opt);
	DeferCall _2([tx]() {delete tx; });
	Cleaner _c;
	_c.push_back([tx]() {tx->Rollback(); });
	
	Status s = _vn_persist_inode(ctx, tx, inode);
	if (!s.ok()) {
		S5LOG_ERROR("Failed to persist inode, for:%s", s.ToString().c_str());
		return -EIO;
	}

	s = tx->Commit();
	if(!s.ok()){
		S5LOG_ERROR("Commit failed, for:%s", s.ToString().c_str());
		return -EIO;
	}
	return 0;
}

inode_no_t vn_ino_of_file(struct ViveFile* f)
{
	return f->i_no;
}

const char* pfs_extent_key::to_string() const
{
	static __thread char str[64];
	snprintf(str, sizeof(str), "[ino:%lld, index:%lld]",  inode_no, extent_index);
	return str;
}

ViveFile::ViveFile():inode(NULL),i_no(0), parent_inode_no(0), noatime(0), nomtime(0), dirty(0)
{

}

ViveFile::~ViveFile()
{
	if(inode)
		vn_dec_inode_ref(inode);
}