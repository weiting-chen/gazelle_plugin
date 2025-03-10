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

#include "codegen/arrow_compute/ext/actions_impl.h"
#include "codegen/arrow_compute/ext/kernels_ext.h"
#include "codegen/arrow_compute/ext/window_sort_kernel.h"

namespace sparkcolumnarplugin {
namespace codegen {
namespace arrowcompute {
namespace extra {

class WindowAggregateFunctionKernel::ActionFactory {
 public:
  ActionFactory(std::shared_ptr<ActionBase> action) { action_ = action; }

  static arrow::Status Make(std::string action_name, arrow::compute::ExecContext* ctx,
                            std::shared_ptr<arrow::DataType> type,
                            std::shared_ptr<arrow::DataType> return_type,
                            std::shared_ptr<ActionFactory>* out) {
    std::shared_ptr<ActionBase> action;
    if (action_name == "sum") {
      RETURN_NOT_OK(MakeSumAction(ctx, type, {return_type}, &action));
    } else if (action_name == "avg") {
      RETURN_NOT_OK(MakeAvgAction(ctx, type, {return_type}, &action));
    } else if (action_name == "min") {
      RETURN_NOT_OK(MakeMinAction(ctx, type, {return_type}, &action));
    } else if (action_name == "max") {
      RETURN_NOT_OK(MakeMaxAction(ctx, type, {return_type}, &action));
    } else if (action_name == "count") {
      RETURN_NOT_OK(MakeCountAction(ctx, {return_type}, &action));
    } else if (action_name == "count_literal") {
      RETURN_NOT_OK(MakeCountLiteralAction(ctx, 1, {return_type},
                                           &action));  // fixme pass literal in
    } else {
      return arrow::Status::Invalid(
          "window aggregate function: unsupported action name: " + action_name);
    }
    *out = std::make_shared<ActionFactory>(action);
    return arrow::Status::OK();
  }

  std::shared_ptr<ActionBase> Get() { return action_; }

 private:
  std::shared_ptr<ActionBase> action_;
};

arrow::Status WindowAggregateFunctionKernel::Make(
    arrow::compute::ExecContext* ctx, std::string function_name,
    std::vector<std::shared_ptr<arrow::DataType>> type_list,
    std::shared_ptr<arrow::DataType> result_type, std::shared_ptr<KernalBase>* out) {
  if (type_list.size() > 1) {
    return arrow::Status::Invalid(
        "given more than 1 input argument for window function: " + function_name);
  }
  std::shared_ptr<ActionFactory> action;

  if (function_name == "sum" || function_name == "avg" || function_name == "min" ||
      function_name == "max" || function_name == "count") {
    RETURN_NOT_OK(
        ActionFactory::Make(function_name, ctx, type_list[0], result_type, &action));
  } else if (function_name == "count_literal") {
    RETURN_NOT_OK(ActionFactory::Make(function_name, ctx, nullptr, result_type, &action));
  } else {
    return arrow::Status::Invalid("window function not supported: " + function_name);
  }
  auto accumulated_group_ids = std::vector<std::shared_ptr<arrow::Int32Array>>();
  *out = std::make_shared<WindowAggregateFunctionKernel>(ctx, type_list, result_type,
                                                         accumulated_group_ids, action);
  return arrow::Status::OK();
}

WindowAggregateFunctionKernel::WindowAggregateFunctionKernel(
    arrow::compute::ExecContext* ctx,
    std::vector<std::shared_ptr<arrow::DataType>> type_list,
    std::shared_ptr<arrow::DataType> result_type,
    std::vector<std::shared_ptr<arrow::Int32Array>> accumulated_group_ids,
    std::shared_ptr<ActionFactory> action) {
  ctx_ = ctx;
  type_list_ = type_list;
  result_type_ = result_type;
  accumulated_group_ids_ = accumulated_group_ids;
  action_ = action;
  kernel_name_ = "WindowAggregateFunctionKernel";
}

/**
 * | a | group |   | group | sum |          | result |
 * | 2 |     0 | + |     0 |   8 |   --->   |      8 |
 * | 3 |     1 |   |     1 |   3 |          |      3 |
 * | 6 |     0 |                            |      8 |
 */
arrow::Status WindowAggregateFunctionKernel::Evaluate(ArrayList& in) {
  // abstract following code to do common inter-window processing

  int32_t max_group_id = 0;
  std::shared_ptr<arrow::Array> group_id_array = in[1];
  auto group_ids = std::dynamic_pointer_cast<arrow::Int32Array>(group_id_array);
  if (group_ids) {
    accumulated_group_ids_.push_back(group_ids);
    for (int i = 0; i < group_ids->length(); i++) {
      if (group_ids->IsNull(i)) {
        continue;
      }
      if (group_ids->GetView(i) > max_group_id) {
        max_group_id = group_ids->GetView(i);
      }
    }
  }
  ArrayList action_input_data;
  action_input_data.push_back(in[0]);
  std::function<arrow::Status(int)> func;
  std::function<arrow::Status()> null_func;
  RETURN_NOT_OK(
      action_->Get()->Submit(action_input_data, max_group_id, &func, &null_func));

  if (group_ids) {
    for (int row_id = 0; row_id < group_id_array->length(); row_id++) {
      if (group_ids->IsNull(row_id)) {
        RETURN_NOT_OK(null_func());
        continue;
      }
      auto group_id = group_ids->GetView(row_id);
      RETURN_NOT_OK(func(group_id));
    }
  }
  return arrow::Status::OK();
}

#define PROCESS_SUPPORTED_TYPES_WINDOW(PROC)                                 \
  PROC(arrow::UInt8Type, arrow::UInt8Builder, arrow::UInt8Array)             \
  PROC(arrow::Int8Type, arrow::Int8Builder, arrow::Int8Array)                \
  PROC(arrow::UInt16Type, arrow::UInt16Builder, arrow::UInt16Array)          \
  PROC(arrow::Int16Type, arrow::Int16Builder, arrow::Int16Array)             \
  PROC(arrow::UInt32Type, arrow::UInt32Builder, arrow::UInt32Array)          \
  PROC(arrow::Int32Type, arrow::Int32Builder, arrow::Int32Array)             \
  PROC(arrow::UInt64Type, arrow::UInt64Builder, arrow::UInt64Array)          \
  PROC(arrow::Int64Type, arrow::Int64Builder, arrow::Int64Array)             \
  PROC(arrow::FloatType, arrow::FloatBuilder, arrow::FloatArray)             \
  PROC(arrow::DoubleType, arrow::DoubleBuilder, arrow::DoubleArray)          \
  PROC(arrow::Date32Type, arrow::Date32Builder, arrow::Date32Array)          \
  PROC(arrow::StringType, arrow::StringBuilder, arrow::StringArray)          \
  PROC(arrow::TimestampType, arrow::TimestampBuilder, arrow::TimestampArray) \
  PROC(arrow::Decimal128Type, arrow::Decimal128Builder, arrow::Decimal128Array)

arrow::Status WindowAggregateFunctionKernel::Finish(ArrayList* out) {
  std::shared_ptr<arrow::DataType> value_type = result_type_;
  switch (value_type->id()) {
#define PROCESS(VALUE_TYPE, BUILDER_TYPE, ARRAY_TYPE)                                \
  case VALUE_TYPE::type_id: {                                                        \
    RETURN_NOT_OK((Finish0<VALUE_TYPE, BUILDER_TYPE, ARRAY_TYPE>(out, value_type))); \
  } break;

    PROCESS_SUPPORTED_TYPES_WINDOW(PROCESS)
#undef PROCESS
    default:
      return arrow::Status::Invalid("window function: unsupported input type: " +
                                    value_type->name());
  }
  return arrow::Status::OK();
}

template <typename ValueType, typename BuilderType, typename ArrayType>
arrow::Status WindowAggregateFunctionKernel::Finish0(
    ArrayList* out, std::shared_ptr<arrow::DataType> data_type) {
  ArrayList action_output;
  RETURN_NOT_OK(action_->Get()->Finish(&action_output));
  if (action_output.size() != 1) {
    return arrow::Status::Invalid(
        "window function: got invalid result from corresponding action");
  }

  auto action_output_values = std::dynamic_pointer_cast<ArrayType>(action_output.at(0));

  for (const auto& accumulated_group_ids_single_part : accumulated_group_ids_) {
    std::shared_ptr<BuilderType> output_builder;
    ARROW_ASSIGN_OR_RAISE(output_builder,
                          (createBuilder<ValueType, BuilderType>(data_type)))

    for (int i = 0; i < accumulated_group_ids_single_part->length(); i++) {
      if (accumulated_group_ids_single_part->IsNull(i)) {
        RETURN_NOT_OK(output_builder->AppendNull());
        continue;
      }
      int32_t group_id = accumulated_group_ids_single_part->GetView(i);
      RETURN_NOT_OK(output_builder->Append(action_output_values->GetView(group_id)));
    }
    std::shared_ptr<arrow::Array> out_array;
    RETURN_NOT_OK(output_builder->Finish(&out_array));
    (*out).push_back(out_array);
  }
  return arrow::Status::OK();
}

template <typename ValueType, typename BuilderType>
typename arrow::enable_if_decimal128<ValueType,
                                     arrow::Result<std::shared_ptr<BuilderType>>>
WindowAggregateFunctionKernel::createBuilder(std::shared_ptr<arrow::DataType> data_type) {
  return std::make_shared<BuilderType>(data_type, ctx_->memory_pool());
}

template <typename ValueType, typename BuilderType>
typename arrow::enable_if_date<ValueType, arrow::Result<std::shared_ptr<BuilderType>>>
WindowAggregateFunctionKernel::createBuilder(std::shared_ptr<arrow::DataType> data_type) {
  return std::make_shared<BuilderType>(ctx_->memory_pool());
}

template <typename ValueType, typename BuilderType>
typename arrow::enable_if_number<ValueType, arrow::Result<std::shared_ptr<BuilderType>>>
WindowAggregateFunctionKernel::createBuilder(std::shared_ptr<arrow::DataType> data_type) {
  return std::make_shared<BuilderType>(ctx_->memory_pool());
}

template <typename ValueType, typename BuilderType>
typename arrow::enable_if_timestamp<ValueType,
                                    arrow::Result<std::shared_ptr<BuilderType>>>
WindowAggregateFunctionKernel::createBuilder(std::shared_ptr<arrow::DataType> data_type) {
  return std::make_shared<BuilderType>(data_type, ctx_->memory_pool());
}

template <typename ValueType, typename BuilderType>
typename arrow::enable_if_string_like<ValueType,
                                      arrow::Result<std::shared_ptr<BuilderType>>>
WindowAggregateFunctionKernel::createBuilder(std::shared_ptr<arrow::DataType> data_type) {
  return std::make_shared<BuilderType>(data_type, ctx_->memory_pool());
}

static arrow::Status EncodeIndices(std::vector<std::shared_ptr<ArrayItemIndexS>> in,
                                   std::shared_ptr<arrow::Array>* out) {
  arrow::UInt64Builder builder;
  for (const auto& each : in) {
    uint64_t encoded = ((uint64_t)(each->array_id) << 32U) ^ ((uint64_t)(each->id));
    RETURN_NOT_OK(builder.Append(encoded));
  }
  RETURN_NOT_OK(builder.Finish(out));
  return arrow::Status::OK();
}

static arrow::Status DecodeIndices(std::shared_ptr<arrow::Array> in,
                                   std::vector<std::shared_ptr<ArrayItemIndexS>>* out) {
  std::vector<std::shared_ptr<ArrayItemIndexS>> v;
  std::shared_ptr<arrow::UInt64Array> selected =
      std::dynamic_pointer_cast<arrow::UInt64Array>(in);
  for (int i = 0; i < selected->length(); i++) {
    uint64_t encoded = selected->GetView(i);
    uint32_t array_id = (encoded & 0xFFFFFFFF00000000U) >> 32U;
    uint32_t id = encoded & 0xFFFFFFFFU;
    v.push_back(std::make_shared<ArrayItemIndexS>(array_id, id));
  }
  *out = v;
  return arrow::Status::OK();
}

arrow::Status WindowSortBase::Evaluate(ArrayList& in) {
  input_cache_.push_back(in);
  return arrow::Status::OK();
}

arrow::Status WindowSortBase::SortToIndicesPrepare(std::vector<ArrayList> values) {
  for (auto each_batch : values) {
    RETURN_NOT_OK(sorter_->Evaluate(each_batch));
  }
  return arrow::Status::OK();
  // todo sort algorithm
}

arrow::Status WindowSortBase::SortToIndicesFinish(
    std::vector<std::shared_ptr<ArrayItemIndexS>> elements_to_sort,
    std::vector<std::shared_ptr<ArrayItemIndexS>>* offsets) {
  std::shared_ptr<arrow::Array> in;
  std::shared_ptr<arrow::Array> out;
  RETURN_NOT_OK(EncodeIndices(elements_to_sort, &in));
  RETURN_NOT_OK(sorter_->Finish(in, &out));
  std::vector<std::shared_ptr<ArrayItemIndexS>> decoded_out;
  RETURN_NOT_OK(DecodeIndices(out, &decoded_out));
  *offsets = decoded_out;
  return arrow::Status::OK();
  // todo sort algorithm
}

arrow::Status WindowSortBase::prepareFinish() {
#ifdef DEBUG
  std::cout << "[window kernel] Entering Rank Kernel's finish method... " << std::endl;
#endif
#ifdef DEBUG
  std::cout << "[window kernel] Splitting all input batches to key/value batches... "
            << std::endl;
#endif
  for (auto batch : input_cache_) {
    ArrayList values_batch;
    for (int i = 0; i < type_list_.size() + 1; i++) {
      auto column_slice = batch.at(i);
      if (i == type_list_.size()) {
        // we are at the column of partition ids
        group_ids_.push_back(std::dynamic_pointer_cast<arrow::Int32Array>(column_slice));
        continue;
      }
      values_batch.push_back(column_slice);
    }
    values_.push_back(values_batch);
    // For getting sort input.
    ArrayList sort_values_batch;
    // See expr_visitor_impl.h, Eval().
    for (int i = type_list_.size() + 1;
         i < type_list_.size() + 1 + order_type_list_.size(); i++) {
      auto column_slice = batch.at(i);
      sort_values_batch.push_back(column_slice);
    }
    sort_values_.push_back(sort_values_batch);
  }
#ifdef DEBUG
  std::cout << "[window kernel] Finished. " << std::endl;
#endif

#ifdef DEBUG
  std::cout << "[window kernel] Calculating max group ID... " << std::endl;
#endif
  for (int i = 0; i < group_ids_.size(); i++) {
    auto slice = group_ids_.at(i);
    for (int j = 0; j < slice->length(); j++) {
      if (slice->IsNull(j)) {
        continue;
      }
      if (slice->GetView(j) > max_group_id_) {
        max_group_id_ = slice->GetView(j);
      }
    }
  }
#ifdef DEBUG
  std::cout << "[window kernel] Finished. " << std::endl;
#endif

  // initialize partitions to be sorted
  std::vector<std::vector<std::shared_ptr<ArrayItemIndexS>>> partitions_to_sort;
  for (int i = 0; i <= max_group_id_; i++) {
    partitions_to_sort.emplace_back();
  }

#ifdef DEBUG
  std::cout << "[window kernel] Creating indexed array based on group IDs... "
            << std::endl;
#endif
  for (int i = 0; i < group_ids_.size(); i++) {
    auto slice = group_ids_.at(i);
    for (int j = 0; j < slice->length(); j++) {
      if (slice->IsNull(j)) {
        continue;
      }
      uint64_t partition_id = slice->GetView(j);
      partitions_to_sort.at(partition_id)
          .push_back(std::make_shared<ArrayItemIndexS>(i, j));
    }
  }
#ifdef DEBUG
  std::cout << "[window kernel] Finished. " << std::endl;
#endif

  RETURN_NOT_OK(SortToIndicesPrepare(sort_values_));
  for (int i = 0; i <= max_group_id_; i++) {
    std::vector<std::shared_ptr<ArrayItemIndexS>> partition = partitions_to_sort.at(i);
    std::vector<std::shared_ptr<ArrayItemIndexS>> sorted_partition;
#ifdef DEBUG
    std::cout << "[window kernel] Sorting a single partition... " << std::endl;
#endif
    RETURN_NOT_OK(SortToIndicesFinish(partition, &sorted_partition));
#ifdef DEBUG
    std::cout << "[window kernel] Finished. " << std::endl;
#endif
    sorted_partitions_.push_back(std::move(sorted_partition));
  }
  // The above is almost as same as Rank's except using sort (order by) input for sorter.
  return arrow::Status::OK();
}

WindowRankKernel::WindowRankKernel(
    arrow::compute::ExecContext* ctx,
    std::vector<std::shared_ptr<arrow::DataType>> type_list,
    std::shared_ptr<WindowSortKernel::Impl> sorter, bool desc,
    std::vector<std::shared_ptr<arrow::DataType>> order_type_list, bool is_row_number) {
  ctx_ = ctx;
  type_list_ = type_list;
  sorter_ = sorter;
  desc_ = desc;
  is_row_number_ = is_row_number;
  order_type_list_ = order_type_list;
}

arrow::Status WindowRankKernel::Make(
    arrow::compute::ExecContext* ctx, std::string function_name,
    std::vector<std::shared_ptr<arrow::DataType>> type_list,
    std::shared_ptr<KernalBase>* out, bool desc,
    std::vector<std::shared_ptr<arrow::DataType>> order_type_list) {
  std::vector<std::shared_ptr<arrow::Field>> key_fields;
  for (int i = 0; i < type_list.size(); i++) {
    key_fields.push_back(
        std::make_shared<arrow::Field>("sort_key" + std::to_string(i), type_list.at(i)));
  }
  std::shared_ptr<arrow::Schema> result_schema =
      std::make_shared<arrow::Schema>(key_fields);

  std::shared_ptr<WindowSortKernel::Impl> sorter;
  // fixme null ordering flag and collation flag
  bool nulls_first = false;
  bool asc = !desc;
  if (key_fields.size() == 1) {
    std::shared_ptr<arrow::Field> key_field = key_fields[0];
    if (key_field->type()->id() == arrow::Type::STRING) {
      sorter.reset(new WindowSortOnekeyKernel<arrow::StringType, std::string>(
          ctx, key_fields, result_schema, nulls_first, asc));
    } else {
      switch (key_field->type()->id()) {
#define PROCESS(InType, BUILDER_TYPE, ARRAY_TYPE)           \
  case InType::type_id: {                                   \
    using CType = typename TypeTraits<InType>::CType;       \
    sorter.reset(new WindowSortOnekeyKernel<InType, CType>( \
        ctx, key_fields, result_schema, nulls_first, asc)); \
  } break;
        PROCESS_SUPPORTED_TYPES_WINDOW(PROCESS)
#undef PROCESS
        default: {
          std::cout << "WindowRankKernel type not supported, type is "
                    << key_field->type() << std::endl;
        } break;
      }
    }
  } else {
    sorter.reset(
        new WindowSortKernel::Impl(ctx, key_fields, result_schema, nulls_first, asc));
    auto status = sorter->LoadJITFunction(key_fields, result_schema);
    if (!status.ok()) {
      std::cout << "LoadJITFunction failed, msg is " << status.message() << std::endl;
      throw JniPendingException("Window Sort codegen failed");
    }
  }
  if (function_name.rfind("row_number", 0) == 0) {
    *out = std::make_shared<WindowRankKernel>(ctx, type_list, sorter, desc,
                                              order_type_list, true);
  } else {
    *out =
        std::make_shared<WindowRankKernel>(ctx, type_list, sorter, desc, order_type_list);
  }

  return arrow::Status::OK();
}

arrow::Status WindowRankKernel::Finish(ArrayList* out) {
  RETURN_NOT_OK(prepareFinish());

  int32_t** rank_array = new int32_t*[group_ids_.size()];
  for (int i = 0; i < group_ids_.size(); i++) {
    *(rank_array + i) = new int32_t[group_ids_.at(i)->length()];
  }
  for (int i = 0; i <= max_group_id_; i++) {
#ifdef DEBUG
    std::cout << "[window kernel] Generating rank result on a single partition... "
              << std::endl;
#endif
    std::vector<std::shared_ptr<ArrayItemIndexS>> sorted_partition =
        sorted_partitions_.at(i);
    int assumed_rank = 0;
    for (int j = 0; j < sorted_partition.size(); j++) {
      ++assumed_rank;  // rank value starts from 1
      std::shared_ptr<ArrayItemIndexS> index = sorted_partition.at(j);
      if (j == 0) {
        rank_array[index->array_id][index->id] = 1;  // rank value starts from 1
        continue;
      }
      std::shared_ptr<ArrayItemIndexS> last_index = sorted_partition.at(j - 1);
      bool same = true;
      for (int column_id = 0; column_id < type_list_.size(); column_id++) {
        bool s = false;
        std::shared_ptr<arrow::DataType> type = type_list_.at(column_id);
        switch (type->id()) {
#define PROCESS(InType, BUILDER_TYPE, ARRAY_TYPE)                                \
  case InType::type_id: {                                                        \
    RETURN_NOT_OK(                                                               \
        AreTheSameValue<ARRAY_TYPE>(values_, column_id, index, last_index, &s)); \
  } break;
          PROCESS_SUPPORTED_TYPES_WINDOW(PROCESS)
#undef PROCESS
          default: {
            std::cout << "WindowRankKernel: type not supported: " << type->ToString()
                      << std::endl;  // todo use arrow::Status
          } break;
        }
        if (!s) {
          same = false;
          break;
        }
      }
      if (same && rank_array[index->array_id] && rank_array[last_index->array_id]) {
        rank_array[index->array_id][index->id] =
            rank_array[last_index->array_id][last_index->id];
        continue;
      }
      rank_array[index->array_id][index->id] = assumed_rank;
    }
#ifdef DEBUG
    std::cout << "[window kernel] Finished. " << std::endl;
#endif
  }

#ifdef DEBUG
  std::cout << "[window kernel] Building overall associated rank results... "
            << std::endl;
#endif
  for (int i = 0; i < input_cache_.size(); i++) {
    auto batch = input_cache_.at(i);
    auto group_id_column_slice = batch.at(type_list_.size());
    int slice_length = group_id_column_slice->length();
    std::shared_ptr<arrow::Int32Builder> rank_builder =
        std::make_shared<arrow::Int32Builder>(ctx_->memory_pool());
    for (int j = 0; j < slice_length; j++) {
      RETURN_NOT_OK(rank_builder->Append(rank_array[i][j]));
    }
    std::shared_ptr<arrow::Int32Array> rank_slice;
    RETURN_NOT_OK(rank_builder->Finish(&rank_slice));
    out->push_back(rank_slice);
  }
#ifdef DEBUG
  std::cout << "[window kernel] Finished. " << std::endl;
#endif
  for (int i = 0; i < group_ids_.size(); i++) {
    delete[] * (rank_array + i);
  }
  delete[] rank_array;
  return arrow::Status::OK();
}

template <typename ArrayType>
arrow::Status WindowRankKernel::AreTheSameValue(const std::vector<ArrayList>& values,
                                                int column,
                                                std::shared_ptr<ArrayItemIndexS> i,
                                                std::shared_ptr<ArrayItemIndexS> j,
                                                bool* out) {
  if (is_row_number_) {
    *out = false;
    return arrow::Status::OK();
  }
  auto typed_array_i =
      std::dynamic_pointer_cast<ArrayType>(values.at(i->array_id).at(column));
  auto typed_array_j =
      std::dynamic_pointer_cast<ArrayType>(values.at(j->array_id).at(column));
  *out = (typed_array_i->GetView(i->id) == typed_array_j->GetView(j->id));
  return arrow::Status::OK();
}

WindowLagKernel::WindowLagKernel(
    arrow::compute::ExecContext* ctx,
    std::vector<std::shared_ptr<arrow::DataType>> type_list,
    std::shared_ptr<WindowSortKernel::Impl> sorter, bool desc, int offset,
    std::shared_ptr<gandiva::LiteralNode> default_node,
    std::shared_ptr<arrow::DataType> return_type,
    std::vector<std::shared_ptr<arrow::DataType>> order_type_list) {
  ctx_ = ctx;
  type_list_ = type_list;
  sorter_ = sorter;
  desc_ = desc;

  offset_ = offset;
  default_node_ = default_node;
  return_type_ = return_type;
  order_type_list_ = order_type_list;
}

// type_list: window function input field; order_type_list: order by field, currently
// multiple fields (> 1) are not well supported.
arrow::Status WindowLagKernel::Make(
    arrow::compute::ExecContext* ctx, std::string function_name,
    std::vector<std::shared_ptr<arrow::DataType>> type_list,
    std::vector<std::shared_ptr<gandiva::LiteralNode>> lag_options,
    std::shared_ptr<KernalBase>* out, bool desc,
    std::shared_ptr<arrow::DataType> return_type,
    std::vector<std::shared_ptr<arrow::DataType>> order_type_list) {
  std::vector<std::shared_ptr<arrow::Field>> key_fields;
  // Use order by field to sort.
  for (int i = 0; i < order_type_list.size(); i++) {
    key_fields.push_back(std::make_shared<arrow::Field>("sort_key" + std::to_string(i),
                                                        order_type_list.at(i)));
  }
  std::shared_ptr<arrow::Schema> result_schema =
      std::make_shared<arrow::Schema>(key_fields);

  std::shared_ptr<WindowSortKernel::Impl> sorter;
  // fixme null ordering flag and collation flag
  bool nulls_first = false;
  bool asc = !desc;
  if (key_fields.size() == 1) {
    std::shared_ptr<arrow::Field> key_field = key_fields[0];
    if (key_field->type()->id() == arrow::Type::STRING) {
      sorter.reset(new WindowSortOnekeyKernel<arrow::StringType, std::string>(
          ctx, key_fields, result_schema, nulls_first, asc));
    } else {
      switch (key_field->type()->id()) {
#define PROCESS(InType, BUILDER_TYPE, ARRAY_TYPE)           \
  case InType::type_id: {                                   \
    using CType = typename TypeTraits<InType>::CType;       \
    sorter.reset(new WindowSortOnekeyKernel<InType, CType>( \
        ctx, key_fields, result_schema, nulls_first, asc)); \
  } break;
        PROCESS_SUPPORTED_TYPES_WINDOW(PROCESS)
#undef PROCESS
        default: {
          std::cout << "type not supported for WindowLagKernel, type is "
                    << key_field->type() << std::endl;
        } break;
      }
    }
  } else {
    sorter.reset(
        new WindowSortKernel::Impl(ctx, key_fields, result_schema, nulls_first, asc));
    auto status = sorter->LoadJITFunction(key_fields, result_schema);
    if (!status.ok()) {
      std::cout << "LoadJITFunction failed, msg is " << status.message() << std::endl;
      throw JniPendingException("Window Sort codegen failed");
    }
  }
  // Currently, only support literal offset value.
  auto offset_value = arrow::util::get<int32_t>(lag_options[0]->holder());
  // Currently, only support literal default value.
  std::shared_ptr<gandiva::LiteralNode> default_node = lag_options[1];
  *out = std::make_shared<WindowLagKernel>(ctx, type_list, sorter, desc, offset_value,
                                           default_node, return_type, order_type_list);

  return arrow::Status::OK();
}

// The interfaces for string type and non-string type are different. So we implemented the
// below two functions which will be passed as argument in HandleSortedPartition.
template <typename ArrayType, typename CType>
CType get_string_value(std::shared_ptr<ArrayType> array, uint32_t index) {
  return array->GetString(index);
}

template <typename ArrayType, typename CType>
CType get_nonstring_value(std::shared_ptr<ArrayType> array, uint32_t index) {
  return array->GetView(index);
}

arrow::Status WindowLagKernel::Finish(ArrayList* out) {
  RETURN_NOT_OK(prepareFinish());

#define PROCESS_SUPPORTED_COMMON_TYPES_LAG(PROC)                    \
  PROC(arrow::UInt8Type, arrow::UInt8Builder, arrow::UInt8Array)    \
  PROC(arrow::Int8Type, arrow::Int8Builder, arrow::Int8Array)       \
  PROC(arrow::UInt16Type, arrow::UInt16Builder, arrow::UInt16Array) \
  PROC(arrow::Int16Type, arrow::Int16Builder, arrow::Int16Array)    \
  PROC(arrow::UInt32Type, arrow::UInt32Builder, arrow::UInt32Array) \
  PROC(arrow::Int32Type, arrow::Int32Builder, arrow::Int32Array)    \
  PROC(arrow::UInt64Type, arrow::UInt64Builder, arrow::UInt64Array) \
  PROC(arrow::Int64Type, arrow::Int64Builder, arrow::Int64Array)    \
  PROC(arrow::FloatType, arrow::FloatBuilder, arrow::FloatArray)    \
  PROC(arrow::DoubleType, arrow::DoubleBuilder, arrow::DoubleArray)

  std::shared_ptr<arrow::DataType> value_type = return_type_;
  switch (value_type->id()) {
#define PROCESS(VALUE_TYPE, BUILDER_TYPE, ARRAY_TYPE)                                  \
  case VALUE_TYPE::type_id: {                                                          \
    using CType = typename arrow::TypeTraits<VALUE_TYPE>::CType;                       \
    RETURN_NOT_OK((HandleSortedPartition<VALUE_TYPE, CType, BUILDER_TYPE, ARRAY_TYPE>( \
        values_, group_ids_, max_group_id_, sorted_partitions_, out,                   \
        get_nonstring_value<ARRAY_TYPE, CType>)));                                     \
  } break;
    PROCESS_SUPPORTED_COMMON_TYPES_LAG(PROCESS)
#undef PROCESS
#undef PROCESS_SUPPORTED_COMMON_TYPES_LAG
    case arrow::StringType::type_id: {
      RETURN_NOT_OK((HandleSortedPartition<arrow::StringType, std::string,
                                           arrow::StringBuilder, arrow::StringArray>(
          values_, group_ids_, max_group_id_, sorted_partitions_, out,
          get_string_value<arrow::StringArray, std::string>)));
    } break;
    default: {
      return arrow::Status::Invalid("window function: unsupported input type: " +
                                    value_type->name());
    } break;
  }
  return arrow::Status::OK();
}

template <typename VALUE_TYPE, typename CType, typename BuilderType, typename ArrayType,
          typename OP>
arrow::Status WindowLagKernel::HandleSortedPartition(
    std::vector<ArrayList>& values,
    std::vector<std::shared_ptr<arrow::Int32Array>>& group_ids, int32_t max_group_id,
    std::vector<std::vector<std::shared_ptr<ArrayItemIndexS>>>& sorted_partitions,
    ArrayList* out, OP op) {
  CType** lag_array = new CType*[group_ids.size()];
  for (int i = 0; i < group_ids.size(); i++) {
    *(lag_array + i) = new CType[group_ids.at(i)->length()];
  }

  // Used to track whether a result is null.
  bool** validity = new bool*[group_ids.size()];
  for (int i = 0; i < group_ids.size(); i++) {
    *(validity + i) = new bool[group_ids.at(i)->length()];
  }

  // Get default value.
  bool is_default_null = default_node_->is_null();
  CType default_value;
  if (!is_default_null) {
    default_value = arrow::util::get<CType>(default_node_->holder());
  }

  for (int i = 0; i <= max_group_id; i++) {
    std::vector<std::shared_ptr<ArrayItemIndexS>> sorted_partition =
        sorted_partitions.at(i);

    for (int j = 0; j < sorted_partition.size(); j++) {
      std::shared_ptr<ArrayItemIndexS> index = sorted_partition.at(j);
      for (int column_id = 0; column_id < type_list_.size(); column_id++) {
        if (j - offset_ < 0 || j - offset_ > sorted_partition.size() - 1) {
          if (is_default_null) {
            validity[index->array_id][index->id] = false;
          } else {
            lag_array[index->array_id][index->id] = default_value;
            validity[index->array_id][index->id] = true;
          }
        } else {
          std::shared_ptr<ArrayItemIndexS> offset_index =
              sorted_partition.at(j - offset_);
          auto typed_array = std::dynamic_pointer_cast<ArrayType>(
              values.at(offset_index->array_id).at(column_id));
          if (typed_array->null_count() > 0 && typed_array->IsNull(offset_index->id)) {
            validity[index->array_id][index->id] = false;
          } else {
            lag_array[index->array_id][index->id] = op(typed_array, offset_index->id);
            validity[index->array_id][index->id] = true;
          }
        }
      }
    }
  }

  for (int i = 0; i < input_cache_.size(); i++) {
    auto batch = input_cache_.at(i);
    auto group_id_column_slice = batch.at(type_list_.size());
    int slice_length = group_id_column_slice->length();
    std::shared_ptr<BuilderType> lag_builder =
        std::make_shared<BuilderType>(ctx_->memory_pool());
    for (int j = 0; j < slice_length; j++) {
      if (validity[i][j]) {
        RETURN_NOT_OK(lag_builder->Append(lag_array[i][j]));
      } else {
        RETURN_NOT_OK(lag_builder->AppendNull());
      }
    }
    std::shared_ptr<ArrayType> lag_slice;
    RETURN_NOT_OK(lag_builder->Finish(&lag_slice));
    out->push_back(lag_slice);
  }

  for (int i = 0; i < group_ids.size(); i++) {
    delete[] * (lag_array + i);
  }
  delete[] lag_array;
  for (int i = 0; i < group_ids.size(); i++) {
    delete[] * (validity + i);
  }
  delete[] validity;

  return arrow::Status::OK();
}

WindowSumKernel::WindowSumKernel(
    arrow::compute::ExecContext* ctx,
    std::vector<std::shared_ptr<arrow::DataType>> type_list,
    std::shared_ptr<WindowSortKernel::Impl> sorter, bool desc,
    std::shared_ptr<arrow::DataType> return_type,
    std::vector<std::shared_ptr<arrow::DataType>> order_type_list) {
  ctx_ = ctx;
  type_list_ = type_list;
  sorter_ = sorter;
  desc_ = desc;

  return_type_ = return_type;
  order_type_list_ = order_type_list;
}

// type_list: window function input field; order_type_list: order by field, currently
// multiple fields (> 1) are not well supported.
arrow::Status WindowSumKernel::Make(
    arrow::compute::ExecContext* ctx, std::string function_name,
    std::vector<std::shared_ptr<arrow::DataType>> type_list,
    std::shared_ptr<KernalBase>* out, bool desc,
    std::shared_ptr<arrow::DataType> return_type,
    std::vector<std::shared_ptr<arrow::DataType>> order_type_list) {
  std::vector<std::shared_ptr<arrow::Field>> key_fields;
  // Use order by field to sort.
  for (int i = 0; i < order_type_list.size(); i++) {
    key_fields.push_back(std::make_shared<arrow::Field>("sort_key" + std::to_string(i),
                                                        order_type_list.at(i)));
  }
  std::shared_ptr<arrow::Schema> result_schema =
      std::make_shared<arrow::Schema>(key_fields);

  std::shared_ptr<WindowSortKernel::Impl> sorter;
  // fixme null ordering flag and collation flag
  bool nulls_first = false;
  bool asc = !desc;
  if (key_fields.size() == 1) {
    std::shared_ptr<arrow::Field> key_field = key_fields[0];
    if (key_field->type()->id() == arrow::Type::STRING) {
      sorter.reset(new WindowSortOnekeyKernel<arrow::StringType, std::string>(
          ctx, key_fields, result_schema, nulls_first, asc));
    } else {
      switch (key_field->type()->id()) {
#define PROCESS(InType, BUILDER_TYPE, ARRAY_TYPE)           \
  case InType::type_id: {                                   \
    using CType = typename TypeTraits<InType>::CType;       \
    sorter.reset(new WindowSortOnekeyKernel<InType, CType>( \
        ctx, key_fields, result_schema, nulls_first, asc)); \
  } break;
        PROCESS_SUPPORTED_TYPES_WINDOW(PROCESS)
#undef PROCESS
        default: {
          std::cout << "type not supported for WindowLagKernel, type is "
                    << key_field->type() << std::endl;
        } break;
      }
    }
  } else {
    sorter.reset(
        new WindowSortKernel::Impl(ctx, key_fields, result_schema, nulls_first, asc));
    auto status = sorter->LoadJITFunction(key_fields, result_schema);
    if (!status.ok()) {
      std::cout << "LoadJITFunction failed, msg is " << status.message() << std::endl;
      throw JniPendingException("Window Sort codegen failed");
    }
  }
  // Currently, only support literal offset value.
  // auto offset_value = arrow::util::get<int32_t>(lag_options[0]->holder());
  // Currently, only support literal default value.
  // std::shared_ptr<gandiva::LiteralNode> default_node = lag_options[1];
  *out = std::make_shared<WindowSumKernel>(ctx, type_list, sorter, desc, return_type,
                                           order_type_list);

  return arrow::Status::OK();
}

arrow::Status WindowSumKernel::Finish(ArrayList* out) {
  RETURN_NOT_OK(prepareFinish());

#define PROCESS_SUPPORTED_COMMON_TYPES_SUM(PROC)                                       \
  PROC(arrow::UInt8Type, arrow::UInt8Array, arrow::Int64Type, arrow::Int64Builder,     \
       arrow::Int64Array)                                                              \
  PROC(arrow::Int8Type, arrow::Int8Array, arrow::Int64Type, arrow::Int64Builder,       \
       arrow::Int64Array)                                                              \
  PROC(arrow::UInt16Type, arrow::UInt16Array, arrow::Int64Type, arrow::Int64Builder,   \
       arrow::Int64Array)                                                              \
  PROC(arrow::Int16Type, arrow::Int16Array, arrow::Int64Type, arrow::Int64Builder,     \
       arrow::Int64Array)                                                              \
  PROC(arrow::UInt32Type, arrow::UInt32Array, arrow::Int64Type, arrow::Int64Builder,   \
       arrow::Int64Array)                                                              \
  PROC(arrow::Int32Type, arrow::Int32Array, arrow::Int64Type, arrow::Int64Builder,     \
       arrow::Int64Array)                                                              \
  PROC(arrow::UInt64Type, arrow::UInt64Array, arrow::Int64Type, arrow::Int64Builder,   \
       arrow::Int64Array)                                                              \
  PROC(arrow::Int64Type, arrow::Int64Array, arrow::Int64Type, arrow::Int64Builder,     \
       arrow::Int64Array)                                                              \
  PROC(arrow::FloatType, arrow::FloatArray, arrow::DoubleType, arrow::DoubleBuilder,   \
       arrow::DoubleArray)                                                             \
  PROC(arrow::DoubleType, arrow::DoubleArray, arrow::DoubleType, arrow::DoubleBuilder, \
       arrow::DoubleArray)

  // For sum, result type can be different from input type. Should NOT be return_type_.
  // Only one element in type_list_, i.e., one col input.
  std::shared_ptr<arrow::DataType> value_type = type_list_[0];
  switch (value_type->id()) {
#define PROCESS(VALUE_TYPE, ARRAY_TYPE, RESULT_TYPE, BUILDER_TYPE, RES_ARRAY_TYPE) \
  case VALUE_TYPE::type_id: {                                                      \
    using CType = typename arrow::TypeTraits<RESULT_TYPE>::CType;                  \
    RETURN_NOT_OK(                                                                 \
        (HandleSortedPartition<ARRAY_TYPE, CType, BUILDER_TYPE, RES_ARRAY_TYPE>(   \
            values_, group_ids_, max_group_id_, sorted_partitions_, out,           \
            get_nonstring_value<ARRAY_TYPE, CType>)));                             \
  } break;
    PROCESS_SUPPORTED_COMMON_TYPES_SUM(PROCESS)
#undef PROCESS
#undef PROCESS_SUPPORTED_COMMON_TYPES_SUM
    default: {
      return arrow::Status::Invalid("window function: unsupported input type: " +
                                    value_type->name());
    } break;
  }
  return arrow::Status::OK();
}

template <typename ArrayType>
bool WindowSumKernel::isSameSortValue(std::shared_ptr<ArrayItemIndexS> curr_array_index,
                                      std::shared_ptr<ArrayItemIndexS> next_array_index,
                                      int col) {
  auto curr_typed_array = std::dynamic_pointer_cast<ArrayType>(
      sort_values_.at(curr_array_index->array_id).at(col));
  auto next_typed_array = std::dynamic_pointer_cast<ArrayType>(
      sort_values_.at(next_array_index->array_id).at(col));
  return (curr_typed_array->GetView(curr_array_index->id) ==
          next_typed_array->GetView(next_array_index->id));
}

// Get the final peer index. In range mode, rows are peers if they have the same values
// for the ORDER BY fields. A frame start of CURRENT ROW refers to the first peer row of
// the current row, while a frame end of CURRENT ROW refers to the last peer row of the
// current row.
int WindowSumKernel::getLastPeerIndex(
    std::vector<std::shared_ptr<ArrayItemIndexS>>& sorted_partition, int curr_index) {
  bool isSame = true;
  int lastPeerIndex = curr_index;
  std::shared_ptr<ArrayItemIndexS> curr_array_index = sorted_partition.at(curr_index);
  for (int i = curr_index + 1; i < sorted_partition.size(); i++) {
    std::shared_ptr<ArrayItemIndexS> next_array_index = sorted_partition.at(i);
    // Compare sort key.
    for (int col = 0; col < order_type_list_.size(); col++) {
      std::shared_ptr<arrow::DataType> value_type = order_type_list_[col];
      switch (value_type->id()) {
#define PROCESS_SUPPORTED_COMMON_TYPES_SORT(PROC) \
  PROC(arrow::UInt8Type, arrow::UInt8Array)       \
  PROC(arrow::Int8Type, arrow::Int8Array)         \
  PROC(arrow::UInt16Type, arrow::UInt16Array)     \
  PROC(arrow::Int16Type, arrow::Int16Array)       \
  PROC(arrow::UInt32Type, arrow::UInt32Array)     \
  PROC(arrow::Int32Type, arrow::Int32Array)       \
  PROC(arrow::UInt64Type, arrow::UInt64Array)     \
  PROC(arrow::Int64Type, arrow::Int64Array)       \
  PROC(arrow::FloatType, arrow::FloatArray)       \
  PROC(arrow::DoubleType, arrow::DoubleArray)

#define PROCESS(VALUE_TYPE, ARRAY_TYPE)                                                 \
  case VALUE_TYPE::type_id: {                                                           \
    isSame =                                                                            \
        isSame && isSameSortValue<ARRAY_TYPE>(curr_array_index, next_array_index, col); \
  } break;
        PROCESS_SUPPORTED_COMMON_TYPES_SORT(PROCESS)
#undef PROCESS
#undef PROCESS_SUPPORTED_COMMON_TYPES_SORT
        case arrow::StringType::type_id: {
          isSame = isSame && isSameSortValue<arrow::StringArray>(curr_array_index,
                                                                 next_array_index, col);
        } break;
        default: {
          throw std::runtime_error("window function: unsupported input type: " +
                                   value_type->name());
        } break;
      }  // switch
      // Jump from the sort col loop.
      if (!isSame) {
        break;
      }
    }  // sort col loop.

    if (isSame) {
      lastPeerIndex = i;
    } else {
      break;
    }
  }  // sorted_partition loop
  return lastPeerIndex;
}

// ArrayType: input ArrayType. CType: result CType. BuilderType: result BuilderType.
// ResArrayType: Result ArrayType.
template <typename ArrayType, typename CType, typename BuilderType, typename ResArrayType,
          typename OP>
arrow::Status WindowSumKernel::HandleSortedPartition(
    std::vector<ArrayList>& values,
    std::vector<std::shared_ptr<arrow::Int32Array>>& group_ids, int32_t max_group_id,
    std::vector<std::vector<std::shared_ptr<ArrayItemIndexS>>>& sorted_partitions,
    ArrayList* out, OP op) {
  CType** sum_array = new CType*[group_ids.size()];
  for (int i = 0; i < group_ids.size(); i++) {
    *(sum_array + i) = new CType[group_ids.at(i)->length()];
  }

  // Used to track whether a result is null.
  bool** validity = new bool*[group_ids.size()];
  for (int i = 0; i < group_ids.size(); i++) {
    *(validity + i) = new bool[group_ids.at(i)->length()];
  }

  for (int i = 0; i <= max_group_id; i++) {
    std::vector<std::shared_ptr<ArrayItemIndexS>> sorted_partition =
        sorted_partitions.at(i);
    CType parition_sum_by_current = (CType)0;
    bool is_valid_value_found = false;
    int j = 0;
    while (j < sorted_partition.size()) {
      std::shared_ptr<ArrayItemIndexS> index = sorted_partition.at(j);
      int column_id = 0;  // One col input.
      auto typed_array =
          std::dynamic_pointer_cast<ArrayType>(values.at(index->array_id).at(column_id));
      // If the first value in one partition (ordered) is null, the result is null.
      // If there is valid value before null, the result for null is as same as the
      // above.
      if (typed_array->null_count() > 0 && typed_array->IsNull(index->id)) {
        if (!is_valid_value_found) {
          validity[index->array_id][index->id] = false;
        } else {
          sum_array[index->array_id][index->id] = parition_sum_by_current;
          validity[index->array_id][index->id] = true;
        }
        j++;
      } else {
        is_valid_value_found = true;
        parition_sum_by_current =
            parition_sum_by_current + (CType)op(typed_array, index->id);
        int lastPeerIndex = getLastPeerIndex(sorted_partition, j);
        // Calculate values with peers considered.
        for (int k = j + 1; k <= lastPeerIndex; k++) {
          std::shared_ptr<ArrayItemIndexS> peer_index = sorted_partition.at(k);
          auto peer_typed_array = std::dynamic_pointer_cast<ArrayType>(
              values.at(peer_index->array_id).at(column_id));
          parition_sum_by_current =
              parition_sum_by_current + (CType)op(peer_typed_array, peer_index->id);
        }
        // Set values for all peers whose sort keys are same in a group.
        for (int k = j; k <= lastPeerIndex; k++) {
          std::shared_ptr<ArrayItemIndexS> peer_index = sorted_partition.at(k);
          auto peer_typed_array = std::dynamic_pointer_cast<ArrayType>(
              values.at(peer_index->array_id).at(column_id));
          sum_array[peer_index->array_id][peer_index->id] = parition_sum_by_current;
          validity[peer_index->array_id][peer_index->id] = true;
        }
        j = lastPeerIndex + 1;
      }
    }
  }

  for (int i = 0; i < input_cache_.size(); i++) {
    auto batch = input_cache_.at(i);
    auto group_id_column_slice = batch.at(type_list_.size());
    int slice_length = group_id_column_slice->length();
    std::shared_ptr<BuilderType> sum_builder =
        std::make_shared<BuilderType>(ctx_->memory_pool());
    for (int j = 0; j < slice_length; j++) {
      if (validity[i][j]) {
        RETURN_NOT_OK(sum_builder->Append(sum_array[i][j]));
      } else {
        RETURN_NOT_OK(sum_builder->AppendNull());
      }
    }
    std::shared_ptr<ResArrayType> sum_slice;
    RETURN_NOT_OK(sum_builder->Finish(&sum_slice));
    out->push_back(sum_slice);
  }

  for (int i = 0; i < group_ids.size(); i++) {
    delete[] * (sum_array + i);
  }
  delete[] sum_array;
  for (int i = 0; i < group_ids.size(); i++) {
    delete[] * (validity + i);
  }
  delete[] validity;

  return arrow::Status::OK();
}

#undef PROCESS_SUPPORTED_TYPES_WINDOW

}  // namespace extra
}  // namespace arrowcompute
}  // namespace codegen
}  // namespace sparkcolumnarplugin