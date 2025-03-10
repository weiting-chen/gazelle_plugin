/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <arrow/array.h>
#include <arrow/status.h>
#include <arrow/type_fwd.h>
#include <arrow/type_traits.h>
#include <arrow/util/iterator.h>
#include <gandiva/node.h>
#include <gandiva/tree_expr_builder.h>

#include <mutex>
#include <thread>

#include "codegen/arrow_compute/ext/array_item_index.h"
#include "codegen/arrow_compute/ext/codegen_context.h"
#include "codegen/common/hash_relation.h"
#include "codegen/common/result_iterator.h"
#include "codegen/common/sort_relation.h"

using ArrayList = std::vector<std::shared_ptr<arrow::Array>>;

namespace sparkcolumnarplugin {
namespace codegen {
namespace arrowcompute {
namespace extra {
class KernalBase {
 public:
  KernalBase() {}
  virtual ~KernalBase() {}
  virtual arrow::Status Spill(int64_t size, int64_t* spilled_size) {
    return arrow::Status::NotImplemented("Spill is abstract interface for ", kernel_name_,
                                         ", output is spill size");
  }
  virtual arrow::Status Evaluate(ArrayList& in) {
    return arrow::Status::NotImplemented("Evaluate is abstract interface for ",
                                         kernel_name_, ", input is arrayList.");
  }
  virtual arrow::Status Evaluate(const ArrayList& in) {
    return arrow::Status::NotImplemented("Evaluate is abstract interface for ",
                                         kernel_name_, ", input is arrayList.");
  }
  virtual arrow::Status Evaluate(arrow::RecordBatchIterator in) {
    return arrow::Status::NotImplemented("Evaluate is abstract interface for ",
                                         kernel_name_, ", input is iterator.");
  }
  virtual arrow::Status Evaluate(const ArrayList& in, ArrayList* out) {
    return arrow::Status::NotImplemented("Evaluate is abstract interface for ",
                                         kernel_name_,
                                         ", input is arrayList, output is arrayList.");
  }
  virtual arrow::Status Evaluate(const ArrayList& in,
                                 const std::shared_ptr<arrow::Array>& dict) {
    return arrow::Status::NotImplemented("Evaluate is abstract interface for ",
                                         kernel_name_, ", input is arrayList and array.");
  }
  virtual arrow::Status Evaluate(const ArrayList& in,
                                 std::shared_ptr<arrow::Array>* out) {
    return arrow::Status::NotImplemented("Evaluate is abstract interface for ",
                                         kernel_name_,
                                         ", input is arrayList, output is array.");
  }
  virtual arrow::Status Evaluate(const std::shared_ptr<arrow::Array>& in,
                                 std::shared_ptr<arrow::Array>* out) {
    return arrow::Status::NotImplemented("Evaluate is abstract interface for ",
                                         kernel_name_,
                                         ", input is array, output is array.");
  }
  virtual std::string GetSignature() { return ""; }
  virtual arrow::Status Finish(ArrayList* out) {
    return arrow::Status::NotImplemented("Finish is abstract interface for ",
                                         kernel_name_, ", output is arrayList");
  }
  virtual arrow::Status Finish(std::shared_ptr<arrow::Array>* out) {
    return arrow::Status::NotImplemented("Finish is abstract interface for ",
                                         kernel_name_, ", output is arrayList");
  }
  virtual arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) {
    return arrow::Status::NotImplemented("MakeResultIterator is abstract interface for ",
                                         kernel_name_);
  }
  virtual arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<HashRelation>>* out) {
    return arrow::Status::NotImplemented("MakeResultIterator is abstract interface for ",
                                         kernel_name_);
  }
  virtual arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<SortRelation>>* out) {
    return arrow::Status::NotImplemented("MakeResultIterator is abstract interface for ",
                                         kernel_name_);
  }
  virtual arrow::Status DoCodeGen(
      int level,
      std::vector<std::pair<std::pair<std::string, std::string>, gandiva::DataTypePtr>>
          input,
      std::shared_ptr<CodeGenContext>* codegen_ctx, int* var_id) {
    return arrow::Status::NotImplemented("DoCodeGen is abstract interface for ",
                                         kernel_name_);
  }

  std::string kernel_name_;
};

class EncodeArrayKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            std::shared_ptr<KernalBase>* out);
  EncodeArrayKernel(arrow::compute::ExecContext* ctx);
  arrow::Status Evaluate(const std::shared_ptr<arrow::Array>& in,
                         std::shared_ptr<arrow::Array>* out) override;

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};

class WindowAggregateFunctionKernel : public KernalBase {
 public:
  class ActionFactory;
  WindowAggregateFunctionKernel(
      arrow::compute::ExecContext* ctx,
      std::vector<std::shared_ptr<arrow::DataType>> type_list,
      std::shared_ptr<arrow::DataType> result_type,
      std::vector<std::shared_ptr<arrow::Int32Array>> accumulated_group_ids,
      std::shared_ptr<ActionFactory> action);
  static arrow::Status Make(arrow::compute::ExecContext* ctx, std::string function_name,
                            std::vector<std::shared_ptr<arrow::DataType>> type_list,
                            std::shared_ptr<arrow::DataType> result_type,
                            std::shared_ptr<KernalBase>* out);
  arrow::Status Evaluate(ArrayList& in) override;
  arrow::Status Finish(ArrayList* out) override;

 private:
  template <typename ValueType, typename BuilderType, typename ArrayType>
  arrow::Status Finish0(ArrayList* out, std::shared_ptr<arrow::DataType> data_type);

  template <typename ValueType, typename BuilderType>
  typename arrow::enable_if_decimal128<ValueType,
                                       arrow::Result<std::shared_ptr<BuilderType>>>
  createBuilder(std::shared_ptr<arrow::DataType> data_type);

  template <typename ValueType, typename BuilderType>
  typename arrow::enable_if_date<ValueType, arrow::Result<std::shared_ptr<BuilderType>>>
  createBuilder(std::shared_ptr<arrow::DataType> data_type);

  template <typename ValueType, typename BuilderType>
  typename arrow::enable_if_number<ValueType, arrow::Result<std::shared_ptr<BuilderType>>>
  createBuilder(std::shared_ptr<arrow::DataType> data_type);

  template <typename ValueType, typename BuilderType>
  typename arrow::enable_if_timestamp<ValueType,
                                      arrow::Result<std::shared_ptr<BuilderType>>>
  createBuilder(std::shared_ptr<arrow::DataType> data_type);

  template <typename ValueType, typename BuilderType>
  typename arrow::enable_if_string_like<ValueType,
                                        arrow::Result<std::shared_ptr<BuilderType>>>
  createBuilder(std::shared_ptr<arrow::DataType> data_type);

  arrow::compute::ExecContext* ctx_ = nullptr;
  std::shared_ptr<ActionFactory> action_;
  std::vector<std::shared_ptr<arrow::Int32Array>> accumulated_group_ids_;
  std::vector<std::shared_ptr<arrow::DataType>> type_list_;
  std::shared_ptr<arrow::DataType> result_type_;
};

class HashArrayKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            std::vector<std::shared_ptr<arrow::DataType>> type_list,
                            std::shared_ptr<KernalBase>* out);
  HashArrayKernel(arrow::compute::ExecContext* ctx,
                  std::vector<std::shared_ptr<arrow::DataType>> type_list);
  arrow::Status Evaluate(const ArrayList& in,
                         std::shared_ptr<arrow::Array>* out) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};

class SortArraysToIndicesKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            std::shared_ptr<arrow::Schema> result_schema,
                            gandiva::NodeVector sort_key_node,
                            std::vector<std::shared_ptr<arrow::Field>> key_field_list,
                            std::vector<bool> sort_directions,
                            std::vector<bool> nulls_order, bool NaN_check,
                            bool do_codegen, int result_type,
                            std::shared_ptr<KernalBase>* out);
  SortArraysToIndicesKernel(arrow::compute::ExecContext* ctx,
                            std::shared_ptr<arrow::Schema> result_schema,
                            gandiva::NodeVector sort_key_node,
                            std::vector<std::shared_ptr<arrow::Field>> key_field_list,
                            std::vector<bool> sort_directions,
                            std::vector<bool> nulls_order, bool NaN_check,
                            bool do_codegen, int result_type);
  arrow::Status Evaluate(ArrayList& in) override;
  arrow::Status Spill(int64_t, int64_t*) override;
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<SortRelation>>* out) override;
  std::string GetSignature() override;

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  std::mutex spill_lck_;
  bool in_spilling_ = false;
  arrow::compute::ExecContext* ctx_ = nullptr;
};

class CachedRelationKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            std::shared_ptr<arrow::Schema> result_schema,
                            std::vector<std::shared_ptr<arrow::Field>> key_field_list,
                            int result_type, std::shared_ptr<KernalBase>* out);
  CachedRelationKernel(arrow::compute::ExecContext* ctx,
                       std::shared_ptr<arrow::Schema> result_schema,
                       std::vector<std::shared_ptr<arrow::Field>> key_field_list,
                       int result_type);
  arrow::Status Evaluate(ArrayList& in) override;
  arrow::Status Evaluate(arrow::RecordBatchIterator in) override;
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<SortRelation>>* out) override;
  std::string GetSignature() override;

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};

class WindowSortKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            std::vector<std::shared_ptr<arrow::Field>> key_field_list,
                            std::shared_ptr<arrow::Schema> result_schema,
                            std::shared_ptr<KernalBase>* out, bool nulls_first, bool asc);
  WindowSortKernel(arrow::compute::ExecContext* ctx,
                   std::vector<std::shared_ptr<arrow::Field>> key_field_list,
                   std::shared_ptr<arrow::Schema> result_schema, bool nulls_first,
                   bool asc);
  arrow::Status Evaluate(const ArrayList& in) override;
  std::string GetSignature() override;

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};

class HashAggregateKernel : public KernalBase {
 public:
  static arrow::Status Make(
      arrow::compute::ExecContext* ctx,
      std::vector<std::shared_ptr<gandiva::Node>> input_field_list,
      std::vector<std::shared_ptr<gandiva::Node>> action_list,
      std::vector<std::shared_ptr<gandiva::Node>> result_field_node_list,
      std::vector<std::shared_ptr<gandiva::Node>> result_expr_node_list,
      std::shared_ptr<KernalBase>* out);
  HashAggregateKernel(arrow::compute::ExecContext* ctx,
                      std::vector<std::shared_ptr<gandiva::Node>> input_field_list,
                      std::vector<std::shared_ptr<gandiva::Node>> action_list,
                      std::vector<std::shared_ptr<gandiva::Node>> result_field_node_list,
                      std::vector<std::shared_ptr<gandiva::Node>> result_expr_node_list);
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  arrow::Status DoCodeGen(
      int level,
      std::vector<std::pair<std::pair<std::string, std::string>, gandiva::DataTypePtr>>
          input,
      std::shared_ptr<CodeGenContext>* codegen_ctx_out, int* var_id) override;
  std::string GetSignature() override;
  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};

// An abstract base class for window functions requiring sort.
class WindowSortBase : public KernalBase {
 public:
  arrow::Status Evaluate(ArrayList& in) override;
  arrow::Status SortToIndicesPrepare(std::vector<ArrayList> values);
  arrow::Status SortToIndicesFinish(
      std::vector<std::shared_ptr<ArrayItemIndexS>> elements_to_sort,
      std::vector<std::shared_ptr<ArrayItemIndexS>>* offsets);
  // For finish preparation, like sorting input fir each group.
  arrow::Status prepareFinish();

 protected:
  std::shared_ptr<WindowSortKernel::Impl> sorter_;
  arrow::compute::ExecContext* ctx_ = nullptr;
  std::vector<ArrayList> input_cache_;
  std::vector<std::shared_ptr<arrow::DataType>> type_list_;
  bool desc_;

  std::vector<std::shared_ptr<arrow::DataType>> order_type_list_;

  std::vector<ArrayList> values_;       // The window function input.
  std::vector<ArrayList> sort_values_;  // Sort input.
  std::vector<std::shared_ptr<arrow::Int32Array>> group_ids_;
  int32_t max_group_id_ = 0;
  std::vector<std::vector<std::shared_ptr<ArrayItemIndexS>>> sorted_partitions_;
};

class WindowRankKernel : public WindowSortBase {
 public:
  WindowRankKernel(arrow::compute::ExecContext* ctx,
                   std::vector<std::shared_ptr<arrow::DataType>> type_list,
                   std::shared_ptr<WindowSortKernel::Impl> sorter, bool desc,
                   std::vector<std::shared_ptr<arrow::DataType>> order_type_list,
                   bool is_row_number = false);
  static arrow::Status Make(
      arrow::compute::ExecContext* ctx, std::string function_name,
      std::vector<std::shared_ptr<arrow::DataType>> type_list,
      std::shared_ptr<KernalBase>* out, bool desc,
      std::vector<std::shared_ptr<arrow::DataType>> order_type_list);
  arrow::Status Finish(ArrayList* out) override;

  template <typename ArrayType>
  arrow::Status AreTheSameValue(const std::vector<ArrayList>& values, int column,
                                std::shared_ptr<ArrayItemIndexS> i,
                                std::shared_ptr<ArrayItemIndexS> j, bool* out);

 protected:
  bool is_row_number_;
};

class WindowLagKernel : public WindowSortBase {
 public:
  WindowLagKernel(arrow::compute::ExecContext* ctx,
                  std::vector<std::shared_ptr<arrow::DataType>> type_list,
                  std::shared_ptr<WindowSortKernel::Impl> sorter, bool desc, int offset,
                  std::shared_ptr<gandiva::LiteralNode> default_node,
                  std::shared_ptr<arrow::DataType> return_type,
                  std::vector<std::shared_ptr<arrow::DataType>> order_type_list);

  static arrow::Status Make(
      arrow::compute::ExecContext* ctx, std::string function_name,
      std::vector<std::shared_ptr<arrow::DataType>> type_list,
      std::vector<std::shared_ptr<gandiva::LiteralNode>> lag_options,
      std::shared_ptr<KernalBase>* out, bool desc,
      std::shared_ptr<arrow::DataType> return_type,
      std::vector<std::shared_ptr<arrow::DataType>> order_type_list);

  arrow::Status Finish(ArrayList* out) override;

  template <typename VALUE_TYPE, typename CType, typename BuilderType, typename ArrayType,
            typename OP>
  arrow::Status HandleSortedPartition(
      std::vector<ArrayList>& values,
      std::vector<std::shared_ptr<arrow::Int32Array>>& group_ids, int32_t max_group_id,
      std::vector<std::vector<std::shared_ptr<ArrayItemIndexS>>>& sorted_partitions,
      ArrayList* out, OP op);

 protected:
  // positive offset means lag to the above row from the current row with an offset.
  // negative offset means lag to the below row from the current row with an offset.
  std::shared_ptr<arrow::DataType> return_type_;
  int offset_;
  std::shared_ptr<gandiva::LiteralNode> default_node_;
};

// For sum window function with sort needed (has to consider window frame).
class WindowSumKernel : public WindowSortBase {
 public:
  WindowSumKernel(arrow::compute::ExecContext* ctx,
                  std::vector<std::shared_ptr<arrow::DataType>> type_list,
                  std::shared_ptr<WindowSortKernel::Impl> sorter, bool desc,
                  std::shared_ptr<arrow::DataType> return_type,
                  std::vector<std::shared_ptr<arrow::DataType>> order_type_list);

  static arrow::Status Make(
      arrow::compute::ExecContext* ctx, std::string function_name,
      std::vector<std::shared_ptr<arrow::DataType>> type_list,
      std::shared_ptr<KernalBase>* out, bool desc,
      std::shared_ptr<arrow::DataType> return_type,
      std::vector<std::shared_ptr<arrow::DataType>> order_type_list);

  arrow::Status Finish(ArrayList* out) override;

  template <typename ArrayType>
  bool isSameSortValue(std::shared_ptr<ArrayItemIndexS> curr_array_index,
                       std::shared_ptr<ArrayItemIndexS> next_array_index, int col);

  int getLastPeerIndex(std::vector<std::shared_ptr<ArrayItemIndexS>>& sorted_partition,
                       int curr_index);

  template <typename VALUE_TYPE, typename CType, typename BuilderType, typename ArrayType,
            typename OP>
  arrow::Status HandleSortedPartition(
      std::vector<ArrayList>& values,
      std::vector<std::shared_ptr<arrow::Int32Array>>& group_ids, int32_t max_group_id,
      std::vector<std::vector<std::shared_ptr<ArrayItemIndexS>>>& sorted_partitions,
      ArrayList* out, OP op);

 protected:
  std::shared_ptr<arrow::DataType> return_type_;
};

/*class UniqueArrayKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            std::shared_ptr<KernalBase>* out);
  UniqueArrayKernel(arrow::compute::ExecContext* ctx);
  arrow::Status Evaluate(const std::shared_ptr<arrow::Array>& in) override;
  arrow::Status Finish(std::shared_ptr<arrow::Array>* out) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_;
};*/

class ConditionedProbeArraysKernel : public KernalBase {
 public:
  static arrow::Status Make(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& left_key_list,
      const std::vector<std::shared_ptr<arrow::Field>>& right_key_list,
      const std::shared_ptr<gandiva::Node>& func_node, int join_type,
      const std::vector<std::shared_ptr<arrow::Field>>& left_field_list,
      const std::vector<std::shared_ptr<arrow::Field>>& right_field_list,
      const std::shared_ptr<arrow::Schema>& result_schema,
      std::shared_ptr<KernalBase>* out);
  ConditionedProbeArraysKernel(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& left_key_list,
      const std::vector<std::shared_ptr<arrow::Field>>& right_key_list,
      const std::shared_ptr<gandiva::Node>& func_node, int join_type,
      const std::vector<std::shared_ptr<arrow::Field>>& left_field_list,
      const std::vector<std::shared_ptr<arrow::Field>>& right_field_list,
      const std::shared_ptr<arrow::Schema>& result_schema);
  arrow::Status Evaluate(const ArrayList& in) override;
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  std::string GetSignature() override;
  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class ConditionedJoinArraysKernel : public KernalBase {
 public:
  static arrow::Status Make(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& left_key_list,
      const std::vector<std::shared_ptr<arrow::Field>>& right_key_list,
      const std::shared_ptr<gandiva::Node>& func_node, int join_type,
      const std::vector<std::shared_ptr<arrow::Field>>& left_field_list,
      const std::vector<std::shared_ptr<arrow::Field>>& right_field_list,
      const std::shared_ptr<arrow::Schema>& result_schema,
      std::shared_ptr<KernalBase>* out);
  ConditionedJoinArraysKernel(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& left_key_list,
      const std::vector<std::shared_ptr<arrow::Field>>& right_key_list,
      const std::shared_ptr<gandiva::Node>& func_node, int join_type,
      const std::vector<std::shared_ptr<arrow::Field>>& left_field_list,
      const std::vector<std::shared_ptr<arrow::Field>>& right_field_list,
      const std::shared_ptr<arrow::Schema>& result_schema);
  arrow::Status Evaluate(ArrayList& in) override;
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  std::string GetSignature() override;
  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class WholeStageCodeGenKernel : public KernalBase {
 public:
  static arrow::Status Make(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& input_field_list,
      std::shared_ptr<gandiva::Node> root_node,
      const std::vector<std::shared_ptr<arrow::Field>>& output_field_list,
      std::shared_ptr<KernalBase>* out);
  WholeStageCodeGenKernel(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& input_field_list,
      std::shared_ptr<gandiva::Node> root_node,
      const std::vector<std::shared_ptr<arrow::Field>>& output_field_list);
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  std::string GetSignature() override;

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class HashRelationKernel : public KernalBase {
 public:
  static arrow::Status Make(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& input_field_list,
      std::shared_ptr<gandiva::Node> root_node,
      const std::vector<std::shared_ptr<arrow::Field>>& output_field_list,
      std::shared_ptr<KernalBase>* out);
  HashRelationKernel(arrow::compute::ExecContext* ctx,
                     const std::vector<std::shared_ptr<arrow::Field>>& input_field_list,
                     std::shared_ptr<gandiva::Node> root_node,
                     const std::vector<std::shared_ptr<arrow::Field>>& output_field_list);
  arrow::Status Evaluate(ArrayList& in) override;
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<HashRelation>>* out) override;
  std::string GetSignature() override;

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class ConcatArrayListKernel : public KernalBase {
 public:
  static arrow::Status Make(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& input_field_list,
      std::shared_ptr<gandiva::Node> root_node,
      const std::vector<std::shared_ptr<arrow::Field>>& output_field_list,
      std::shared_ptr<KernalBase>* out);
  ConcatArrayListKernel(
      arrow::compute::ExecContext* ctx,
      const std::vector<std::shared_ptr<arrow::Field>>& input_field_list,
      std::shared_ptr<gandiva::Node> root_node,
      const std::vector<std::shared_ptr<arrow::Field>>& output_field_list);
  arrow::Status Evaluate(const ArrayList& in) override;
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;

  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class ConditionedProbeKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            const gandiva::NodeVector& left_key_list,
                            const gandiva::NodeVector& right_key_list,
                            const gandiva::NodeVector& left_schema_list,
                            const gandiva::NodeVector& right_schema_list,
                            const gandiva::NodePtr& condition, int join_type,
                            bool is_null_aware_anti_join,
                            const gandiva::NodeVector& result_schema,
                            const gandiva::NodeVector& hash_configuration_list,
                            int hash_relation_idx, std::shared_ptr<KernalBase>* out);
  ConditionedProbeKernel(arrow::compute::ExecContext* ctx,
                         const gandiva::NodeVector& left_key_list,
                         const gandiva::NodeVector& right_key_list,
                         const gandiva::NodeVector& left_schema_list,
                         const gandiva::NodeVector& right_schema_list,
                         const gandiva::NodePtr& condition, int join_type,
                         bool is_null_aware_anti_join,
                         const gandiva::NodeVector& result_schema,
                         const gandiva::NodeVector& hash_configuration_list,
                         int hash_relation_idx);
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  arrow::Status DoCodeGen(
      int level,
      std::vector<std::pair<std::pair<std::string, std::string>, gandiva::DataTypePtr>>
          input,
      std::shared_ptr<CodeGenContext>* codegen_ctx_out, int* var_id) override;
  std::string GetSignature() override;
  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class ConditionedMergeJoinKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            const gandiva::NodeVector& left_key_list,
                            const gandiva::NodeVector& right_key_list,
                            const gandiva::NodeVector& left_schema_list,
                            const gandiva::NodeVector& right_schema_list,
                            const gandiva::NodePtr& condition, int join_type,
                            const gandiva::NodeVector& result_schema,
                            std::vector<int> hash_relation_idx,
                            std::shared_ptr<KernalBase>* out);
  ConditionedMergeJoinKernel(arrow::compute::ExecContext* ctx,
                             const gandiva::NodeVector& left_key_list,
                             const gandiva::NodeVector& right_key_list,
                             const gandiva::NodeVector& left_schema_list,
                             const gandiva::NodeVector& right_schema_list,
                             const gandiva::NodePtr& condition, int join_type,
                             const gandiva::NodeVector& result_schema,
                             std::vector<int> hash_relation_idx);
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  arrow::Status DoCodeGen(
      int level,
      std::vector<std::pair<std::pair<std::string, std::string>, gandiva::DataTypePtr>>
          input,
      std::shared_ptr<CodeGenContext>* codegen_ctx_out, int* var_id) override;
  std::string GetSignature() override;
  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class ProjectKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            const gandiva::NodeVector& input_field_node_list,
                            const gandiva::NodeVector& project_list,
                            std::shared_ptr<KernalBase>* out);
  ProjectKernel(arrow::compute::ExecContext* ctx,
                const gandiva::NodeVector& input_field_node_list,
                const gandiva::NodeVector& project_list);
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  arrow::Status DoCodeGen(
      int level,
      std::vector<std::pair<std::pair<std::string, std::string>, gandiva::DataTypePtr>>
          input,
      std::shared_ptr<CodeGenContext>* codegen_ctx, int* var_id) override;
  std::string GetSignature() override;
  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class FilterKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            const gandiva::NodeVector& input_field_node_list,
                            const gandiva::NodePtr& condition,
                            std::shared_ptr<KernalBase>* out);
  FilterKernel(arrow::compute::ExecContext* ctx,
               const gandiva::NodeVector& input_field_node_list,
               const gandiva::NodePtr& condition);
  arrow::Status MakeResultIterator(
      std::shared_ptr<arrow::Schema> schema,
      std::shared_ptr<ResultIterator<arrow::RecordBatch>>* out) override;
  arrow::Status DoCodeGen(
      int level,
      std::vector<std::pair<std::pair<std::string, std::string>, gandiva::DataTypePtr>>
          input,
      std::shared_ptr<CodeGenContext>* codegen_ctx, int* var_id) override;
  std::string GetSignature() override;
  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
class ConcatArrayKernel : public KernalBase {
 public:
  static arrow::Status Make(arrow::compute::ExecContext* ctx,
                            std::vector<std::shared_ptr<arrow::DataType>> type_list,
                            std::shared_ptr<KernalBase>* out);
  ConcatArrayKernel(arrow::compute::ExecContext* ctx,
                    std::vector<std::shared_ptr<arrow::DataType>> type_list);
  arrow::Status Evaluate(const ArrayList& in,
                         std::shared_ptr<arrow::Array>* out) override;
  class Impl;

 private:
  std::unique_ptr<Impl> impl_;
  arrow::compute::ExecContext* ctx_ = nullptr;
};
}  // namespace extra
}  // namespace arrowcompute
}  // namespace codegen
}  // namespace sparkcolumnarplugin
