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

using namespace std;
using namespace ROCKSDB_NAMESPACE;


class ViveDataMergeOperator : public MergeOperator
{
public:
	virtual bool FullMerge(const Slice& key,
		const Slice* existing_value,
		const std::deque<std::string>& operand_list,
		std::string* new_value,
		Logger* logger) const override;
	virtual bool PartialMerge(const Slice& key,
		const Slice& left_operand,
		const Slice& right_operand,
		std::string* new_value,
		Logger* logger) const override;
	virtual bool AllowSingleOperand() const { return true; }
	virtual const char* Name() const override {
		return "ViveDataMergeOperator";
	}
};
static inline void set_bit(int16_t& bmp, int index)
{
	bmp |= (1 << index);
}
static inline int is_bit_clean(int16_t& bmp, int index) {
	return (bmp & (1 << index)) == 0;
}
static inline int is_bit_set(int16_t& bmp, int index)
{
	return (bmp & (1 << index)) != 0;
}


bool ViveDataMergeOperator::FullMerge(const Slice& key,
	const Slice* existing_value,
	const std::deque<std::string>& operand_list,
	std::string* new_value,
	Logger* logger) const override
{
	new_value->resize((64 << 10) + sizeof(struct pfs_extent_head));
	char* new_buf = new_value->data();
	char* new_data_buf = new_buf + sizeof(struct pfs_extent_head);
	struct pfs_extent_head* existing_ext_head = existing_value->data();
	char* existing_data_buf = ((char*)existing_ext_head) + sizeof(struct pfs_extent_head);

	assert(existing_ext_head->data_bmp == 0xffff);//suppose base data are full filled
	for (const std::string& value : operand_list) {
		int in_op_off = 0;
		char* buf = value.data();
		struct pfs_extent_head* ext_head = buf;
		char* data_buf = buf + sizeof(struct pfs_extent_head);
		memcpy(new_data_buf + ext_head->merge_off, data_buf, value.length() - sizeof(struct pfs_extent_head));
	}
	return true;
}
bool ViveDataMergeOperator::PartialMerge(const Slice& key,
	const Slice& left_operand,
	const Slice& right_operand,
	std::string* new_value,
	Logger* logger) const override
{
	struct pfs_extent_head* left_ext_head = left_operand->data();
	char* left_data_buf = ((char*)left_ext_head) + sizeof(struct pfs_extent_head);
	struct pfs_extent_head* right_ext_head = right_operand->data();
	char* right_data_buf = ((char*)right_ext_head) + sizeof(struct pfs_extent_head);


	size_t ext_begin = min(left_ext_head->merge_off, right_ext_head->merge_off);
	size_t ext_end = max(left_ext_head->merge_off + left_operand.size() - sizeof(struct pfs_extent_head),
		right_ext_head->merge_off + right_operand.size() - sizeof(struct pfs_extent_head));

	new_value->resize(ext_end - ext_begin + sizeof(struct pfs_extent_head));
	struct pfs_extent_head* new_ext_head = new_value->data();
	char* new_data_buf = ((char*)new_ext_head) + sizeof(struct pfs_extent_head);
	memcpy(new_data_buf + (left_ext_head->merge_off - ext_begin), left_data_buf, left_operand.size() - sizeof(struct pfs_extent_head));
	memcpy(new_data_buf + (right_ext_head->merge_off - ext_begin), right_data_buf, right_operand.size() - sizeof(struct pfs_extent_head));
	new_ext_head->merge_off = ext_begin;
	return true;
}