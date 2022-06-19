#ifndef data_merge_h__
#define data_merge_h__

#include <rocksdb/merge_operator.h>
using ROCKSDB_NAMESPACE::MergeOperator;
using ROCKSDB_NAMESPACE::Slice;
using ROCKSDB_NAMESPACE::Logger;

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
#endif // data_merge_h__