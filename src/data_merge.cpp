#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"
#include "rocksdb/merge_operator.h"
#include <rocksdb/utilities/transaction.h>
#include <fcntl.h>

#include "vivenas.h"
#include <nlohmann/json.hpp>
#include <string>
#include "pf_utils.h"
#include "vivenas.h"
#include "vivenas_internal.h"
#include "data_merge.h"

using namespace std;
using namespace ROCKSDB_NAMESPACE;



static inline void set_bit(int16_t& bmp, int index)
{
	bmp |= (int16_t)(1 << index);
}
static inline int is_bit_clean(int16_t& bmp, int index) {
	return (bmp & (1 << index)) == 0;
}
static inline int is_bit_set(int16_t& bmp, int index)
{
	return (bmp & (1 << index)) != 0;
}

#if 0
bool ViveDataMergeOperator::FullMerge(const Slice& key,
	const Slice* existing_value,
	const std::deque<std::string>& operand_list,
	std::string* new_value,
	Logger* logger) const
{

	const struct pfs_extent_key *ext_key = (const struct pfs_extent_key* )key.data();
	S5LOG_DEBUG("FullMerge extent:%s with %d operand", ext_key->to_string(), operand_list.size());
	new_value->resize(VIVEFS_EXTENT_SIZE + sizeof(struct pfs_extent_head));
	char* new_buf = new_value->data();
	char* new_data_buf = new_buf + sizeof(struct pfs_extent_head);
	if(existing_value != NULL){
		S5LOG_DEBUG("FullMerge with existing_value size %ld", existing_value->size());
		const struct pfs_extent_head* existing_ext_head = (const struct pfs_extent_head* )existing_value->data();
		const char* existing_data_buf = existing_value->data() + sizeof(struct pfs_extent_head);
		assert(existing_ext_head->data_bmp == 0xffff);//suppose base data are full filled
		memcpy(new_data_buf , existing_data_buf, existing_value->size() - sizeof(struct pfs_extent_head));

	}

	int i=0;
	for (const std::string& value : operand_list) {
		if(value.length() == 0){
			S5LOG_DEBUG("skip operand[%d] whose length is 0", i);
			continue;
		}
		const char* buf = value.data();
		const struct pfs_extent_head* ext_head = (const struct pfs_extent_head* )buf;
		const char* data_buf = buf + sizeof(struct pfs_extent_head);
		memcpy(new_data_buf + ext_head->merge_off, data_buf, value.length() - sizeof(struct pfs_extent_head));
		i++;
	}
	return true;
}
bool ViveDataMergeOperator::PartialMerge(const Slice& key,
	const Slice& left_operand,
	const Slice& right_operand,
	std::string* new_value,
	Logger* logger) const
{
	const struct pfs_extent_head* left_ext_head = (const struct pfs_extent_head* )left_operand.data();
	const char* left_data_buf = ((const char*)left_ext_head) + sizeof(struct pfs_extent_head);
	const struct pfs_extent_head* right_ext_head = (const struct pfs_extent_head*)right_operand.data();
	char* right_data_buf = ((char*)right_ext_head) + sizeof(struct pfs_extent_head);
	const struct pfs_extent_key* ext_key = (const struct pfs_extent_key*)key.data();
	S5LOG_DEBUG("PartialMerge extent:%s with left:%d + right:%d bytes", ext_key->to_string(), 
		left_operand.size() - sizeof(struct pfs_extent_head),
		right_operand.size() - sizeof(struct pfs_extent_head));


	size_t ext_begin = min(left_ext_head->merge_off, right_ext_head->merge_off);
	size_t ext_end = max(left_ext_head->merge_off + left_operand.size() - sizeof(struct pfs_extent_head),
		right_ext_head->merge_off + right_operand.size() - sizeof(struct pfs_extent_head));

	new_value->resize(ext_end - ext_begin + sizeof(struct pfs_extent_head));
	struct pfs_extent_head* new_ext_head = (struct pfs_extent_head* )new_value->data();
	char* new_data_buf = ((char*)new_ext_head) + sizeof(struct pfs_extent_head);
	memcpy(new_data_buf + (left_ext_head->merge_off - ext_begin), left_data_buf, left_operand.size() - sizeof(struct pfs_extent_head));
	memcpy(new_data_buf + (right_ext_head->merge_off - ext_begin), right_data_buf, right_operand.size() - sizeof(struct pfs_extent_head));
	new_ext_head->merge_off = (int16_t)ext_begin;
	return true;
}


bool ViveDataMergeOperator::PartialMergeMulti(const Slice& key,
	const std::deque<Slice>& operand_list,
	std::string* new_value,
	Logger* logger) const {
	S5LOG_DEBUG("delegate PartialMergeMulti with %d operand", operand_list.size());
	if(operand_list.size() == 1){
		*new_value = operand_list[0].data();
		return true;
	}
	return MergeOperator::PartialMergeMulti(key, operand_list, new_value, logger);

}

bool ViveDataMergeOperator::FullMergeV2(const MergeOperationInput& merge_in,
	MergeOperationOutput* merge_out) const
{
	const struct pfs_extent_key* ext_key = (const struct pfs_extent_key*)merge_in.key.data();
	S5LOG_DEBUG("FullMergeV2 extent:%s with %d operand", ext_key->to_string(), merge_in.operand_list.size());

	merge_out->new_value.resize(VIVEFS_EXTENT_SIZE + sizeof(struct pfs_extent_head));
	char* new_buf = merge_out->new_value.data();
	char* new_data_buf = new_buf + sizeof(struct pfs_extent_head);
	if (merge_in.existing_value != NULL) {
		S5LOG_DEBUG("FullMergeV2 with existing_value size %ld", merge_in.existing_value->size());
		const struct pfs_extent_head* existing_ext_head = (const struct pfs_extent_head*)merge_in.existing_value->data();
		const char* existing_data_buf = merge_in.existing_value->data() + sizeof(struct pfs_extent_head);
		assert(existing_ext_head->data_bmp == 0xffff);//suppose base data are full filled
		memcpy(new_data_buf, existing_data_buf, merge_in.existing_value->size() - sizeof(struct pfs_extent_head));

	}


	int i = 0;
	for (const Slice& value : merge_in.operand_list) {
		if (value.size() == 0) {
			S5LOG_DEBUG("skip operand[%d] whose length is 0", i++);
			continue;
		}
		const char* buf = value.data();
		const struct pfs_extent_head* ext_head = (const struct pfs_extent_head*)buf;
		const char* data_buf = buf + sizeof(struct pfs_extent_head);
		memcpy(new_data_buf + ext_head->merge_off, data_buf, value.size() - sizeof(struct pfs_extent_head));
		i++;
	}

	return true;
}
#else
bool ViveDataMergeOperator::Merge(const Slice& key,
	const Slice* existing_value,
	const Slice& value,
	std::string* new_value,
	Logger* logger) const
{
	if(existing_value == NULL){
		new_value->resize(value.size());
		memcpy(new_value->data(), value.data(), value.size());
		return true;
	}
	const struct pfs_extent_head* left_ext_head = (const struct pfs_extent_head*)existing_value->data();
	const char* left_data_buf = ((const char*)left_ext_head) + sizeof(struct pfs_extent_head);

	const struct pfs_extent_head* right_ext_head = (const struct pfs_extent_head*)value.data();
	char* right_data_buf = ((char*)right_ext_head) + sizeof(struct pfs_extent_head);
	const struct pfs_extent_key* ext_key = (const struct pfs_extent_key*)key.data();
	S5LOG_DEBUG("Merge extent:%s with left:%d + right:%d bytes", ext_key->to_string(),
		existing_value->size() - sizeof(struct pfs_extent_head),
		value.size() - sizeof(struct pfs_extent_head));


	size_t ext_begin = min(left_ext_head->merge_off, right_ext_head->merge_off);
	size_t ext_end = max(left_ext_head->merge_off + existing_value->size() - sizeof(struct pfs_extent_head),
		right_ext_head->merge_off + value.size() - sizeof(struct pfs_extent_head));

	new_value->resize(ext_end - ext_begin + sizeof(struct pfs_extent_head));
	struct pfs_extent_head* new_ext_head = (struct pfs_extent_head*)new_value->data();
	char* new_data_buf = ((char*)new_ext_head) + sizeof(struct pfs_extent_head);
	memcpy(new_data_buf + (left_ext_head->merge_off - ext_begin), left_data_buf, existing_value->size() - sizeof(struct pfs_extent_head));
	memcpy(new_data_buf + (right_ext_head->merge_off - ext_begin), right_data_buf, value.size() - sizeof(struct pfs_extent_head));
	new_ext_head->merge_off = (int16_t)ext_begin;
	return true;
}
#endif
//思路1： 测试不使用merge, 只使用Put
//      这样操作在Ctr-C后重启，文件内容仍然在, 说明不是pfAOF的问题。
//思路2： 实现FullMergeV2
//      没有作用，重启后文件内容为空
//思路3： 实现AssociativeMergeOperator：：Merge接口
//      这个方法可行！