#ifndef data_merge_h__
#define data_merge_h__

#include <rocksdb/merge_operator.h>
using ROCKSDB_NAMESPACE::MergeOperator;
using ROCKSDB_NAMESPACE::Slice;
using ROCKSDB_NAMESPACE::Logger;
using ROCKSDB_NAMESPACE::AssociativeMergeOperator;
#ifdef USE_ASSOCIATE_MERGE 
class ViveDataMergeOperator : public AssociativeMergeOperator
{
public:
	//virtual bool FullMerge(const Slice& key,
	//	const Slice* existing_value,
	//	const std::deque<std::string>& operand_list,
	//	std::string* new_value,
	//	Logger* logger) const override;
	//virtual bool PartialMerge(const Slice& key,
	//	const Slice& left_operand,
	//	const Slice& right_operand,
	//	std::string* new_value,
	//	Logger* logger) const override;
	//virtual bool PartialMergeMulti(const Slice& key,
	//	const std::deque<Slice>& operand_list,
	//	std::string* new_value,
	//	Logger* logger) const;
	//virtual bool FullMergeV2(const MergeOperationInput& merge_in,
	//	MergeOperationOutput* merge_out) const override;
	//virtual bool AllowSingleOperand() const  override { return true; }
	//virtual bool ShouldMerge(const std::vector<Slice>& /*operands*/) const override  { return true; }

	virtual bool Merge(const Slice& key,
		const Slice* existing_value,
		const Slice& value,
		std::string* new_value,
		Logger* logger) const;

	virtual const char* Name() const {
		return "ViveDataMergeOperator";
	}
};
#else
class ViveDataMergeOperator : public MergeOperator
{
public:
	//virtual bool FullMerge(const Slice& key,
	//	const Slice* existing_value,
	//	const std::deque<std::string>& operand_list,
	//	std::string* new_value,
	//	Logger* logger) const override;
	virtual bool PartialMerge(const Slice& key,
		const Slice& left_operand,
		const Slice& right_operand,
		std::string* new_value,
		Logger* logger) const override;
	//virtual bool PartialMergeMulti(const Slice& key,
	//	const std::deque<Slice>& operand_list,
	//	std::string* new_value,
	//	Logger* logger) const;
	virtual bool FullMergeV2(const MergeOperationInput& merge_in,
		MergeOperationOutput* merge_out) const override;
	virtual bool AllowSingleOperand() const  override { return true; }
	virtual bool ShouldMerge(const std::vector<Slice>& /*operands*/) const override  { return true; }


	virtual const char* Name() const {
		return "ViveDataMergeOperator";
	}
};
#endif
#endif // data_merge_h__