//===- InlineModelFeatureMaps.h - common model runner defs ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_ANALYSIS_INLINEMODELFEATUREMAPS_H
#define LLVM_ANALYSIS_INLINEMODELFEATUREMAPS_H

#include "llvm/Analysis/TensorSpec.h"

#include <array>
#include <string>
#include <vector>

namespace llvm {

// List of cost features. A "cost" feature is a summand of the heuristic-based
// inline cost, and we define them separately to preserve the original heuristic
// behavior.
#define INLINE_COST_FEATURE_ITERATOR(M)                                        \
  M(int64_t, {1}, sroa_savings,                                                \
    "Savings from SROA (scalar replacement of aggregates)")                    \
  M(int64_t, {1}, sroa_losses,                                                 \
    "Losses from SROA (scalar replacement of aggregates)")                     \
  M(int64_t, {1}, load_elimination, "Cost of load elimination in the call")    \
  M(int64_t, {1}, call_penalty,                                                \
    "Accumulation of penalty applied to call sites when inlining")             \
  M(int64_t, {1}, call_argument_setup,                                         \
    "Accumulation of call argument setup costs")                               \
  M(int64_t, {1}, load_relative_intrinsic,                                     \
    "Accumulation of costs of loading relative intrinsics")                    \
  M(int64_t, {1}, lowered_call_arg_setup,                                      \
    "Accumulation of cost of lowered call argument setups")                    \
  M(int64_t, {1}, indirect_call_penalty,                                       \
    "Accumulation of costs for indirect calls")                                \
  M(int64_t, {1}, jump_table_penalty, "Accumulation of costs for jump tables") \
  M(int64_t, {1}, case_cluster_penalty,                                        \
    "Accumulation of costs for case clusters")                                 \
  M(int64_t, {1}, switch_penalty,                                              \
    "Accumulation of costs for switch statements")                             \
  M(int64_t, {1}, unsimplified_common_instructions,                            \
    "Costs from unsimplified common instructions")                             \
  M(int64_t, {1}, num_loops, "Number of loops in the caller")                  \
  M(int64_t, {1}, dead_blocks, "Number of dead blocks in the caller")          \
  M(int64_t, {1}, simplified_instructions,                                     \
    "Number of simplified instructions")                                       \
  M(int64_t, {1}, constant_args,                                               \
    "Number of constant arguments in the call site")                           \
  M(int64_t, {1}, constant_offset_ptr_args,                                    \
    "Number of constant offset pointer args in the call site")                 \
  M(int64_t, {1}, callsite_cost, "Estimated cost of the call site")            \
  M(int64_t, {1}, cold_cc_penalty, "Penalty for a cold calling convention")    \
  M(int64_t, {1}, last_call_to_static_bonus,                                   \
    "Bonus for being the last call to static")                                 \
  M(int64_t, {1}, is_multiple_blocks,                                          \
    "Boolean; is the Callee multiple blocks")                                  \
  M(int64_t, {1}, nested_inlines,                                              \
    "Would the default inliner perfom nested inlining")                        \
  M(int64_t, {1}, nested_inline_cost_estimate,                                 \
    "Estimate of the accumulated cost of nested inlines")                      \
  M(int64_t, {1}, threshold, "Threshold for the heuristic inliner")

// clang-format off
enum class InlineCostFeatureIndex : size_t {
#define POPULATE_INDICES(DTYPE, SHAPE, NAME, DOC) NAME,
  INLINE_COST_FEATURE_ITERATOR(POPULATE_INDICES)
#undef POPULATE_INDICES

  NumberOfFeatures
};

#if defined(ENABLE_ACPO)
const std::map<InlineCostFeatureIndex, std::string> InlineCostFeatureIndexToName = {
  { InlineCostFeatureIndex::sroa_savings, "sroa_savings" },
  { InlineCostFeatureIndex::sroa_losses, "sroa_losses" },
  { InlineCostFeatureIndex::load_elimination, "load_elimination" },
  { InlineCostFeatureIndex::call_penalty, "call_penalty" },
  { InlineCostFeatureIndex::call_argument_setup, "call_argument_setup" },
  { InlineCostFeatureIndex::load_relative_intrinsic, "load_relative_intrinsic" },
  { InlineCostFeatureIndex::lowered_call_arg_setup, "lowered_call_arg_setup" },
  { InlineCostFeatureIndex::indirect_call_penalty, "indirect_call_penalty" },
  { InlineCostFeatureIndex::jump_table_penalty, "jump_table_penalty" },
  { InlineCostFeatureIndex::case_cluster_penalty, "case_cluster_penalty" },
  { InlineCostFeatureIndex::switch_penalty, "switch_penalty" },
  { InlineCostFeatureIndex::unsimplified_common_instructions, "unsimplified_common_instructions" },
  { InlineCostFeatureIndex::num_loops, "num_loops" },
  { InlineCostFeatureIndex::dead_blocks, "dead_blocks" },
  { InlineCostFeatureIndex::simplified_instructions, "simplified_instructions" },
  { InlineCostFeatureIndex::constant_args, "constant_args" },
  { InlineCostFeatureIndex::constant_offset_ptr_args, "constant_offset_ptr_args" },
  { InlineCostFeatureIndex::callsite_cost, "callsite_cost" },
  { InlineCostFeatureIndex::cold_cc_penalty, "cold_cc_penalty" },
  { InlineCostFeatureIndex::last_call_to_static_bonus, "last_call_to_static_bonus" },
  { InlineCostFeatureIndex::is_multiple_blocks, "is_multiple_blocks" },
  { InlineCostFeatureIndex::nested_inlines, "nested_inlines" },
  { InlineCostFeatureIndex::nested_inline_cost_estimate, "nested_inline_cost_estimate" },
  { InlineCostFeatureIndex::threshold, "threshold" }
};
#endif

// clang-format on

using InlineCostFeatures =
    std::array<int,
               static_cast<size_t>(InlineCostFeatureIndex::NumberOfFeatures)>;

constexpr bool isHeuristicInlineCostFeature(InlineCostFeatureIndex Feature) {
  return Feature != InlineCostFeatureIndex::sroa_savings &&
         Feature != InlineCostFeatureIndex::is_multiple_blocks &&
         Feature != InlineCostFeatureIndex::dead_blocks &&
         Feature != InlineCostFeatureIndex::simplified_instructions &&
         Feature != InlineCostFeatureIndex::constant_args &&
         Feature != InlineCostFeatureIndex::constant_offset_ptr_args &&
         Feature != InlineCostFeatureIndex::nested_inlines &&
         Feature != InlineCostFeatureIndex::nested_inline_cost_estimate &&
         Feature != InlineCostFeatureIndex::threshold;
}

// List of features. Each feature is defined through a triple:
// - the name of an enum member, which will be the feature index
// - a textual name, used for Tensorflow model binding (so it needs to match the
// names used by the Tensorflow model)
// - a documentation description. Currently, that is not used anywhere
// programmatically, and serves as workaround to inability of inserting comments
// in macros.
#define INLINE_FEATURE_ITERATOR(M)                                             \
  M(int64_t, {1}, callee_basic_block_count,                                    \
    "number of basic blocks of the callee")                                    \
  M(int64_t, {1}, callsite_height,                                             \
    "position of the call site in the original call graph - measured from "    \
    "the farthest SCC")                                                        \
  M(int64_t, {1}, node_count,                                                  \
    "total current number of defined functions in the module")                 \
  M(int64_t, {1}, nr_ctant_params,                                             \
    "number of parameters in the call site that are constants")                \
  M(int64_t, {1}, cost_estimate, "total cost estimate (threshold - free)")     \
  M(int64_t, {1}, edge_count, "total number of calls in the module")           \
  M(int64_t, {1}, caller_users,                                                \
    "number of module-internal users of the caller, +1 if the caller is "      \
    "exposed externally")                                                      \
  M(int64_t, {1}, caller_conditionally_executed_blocks,                        \
    "number of blocks reached from a conditional instruction, in the caller")  \
  M(int64_t, {1}, caller_basic_block_count,                                    \
    "number of basic blocks in the caller")                                    \
  M(int64_t, {1}, callee_conditionally_executed_blocks,                        \
    "number of blocks reached from a conditional instruction, in the callee")  \
  M(int64_t, {1}, callee_users,                                                \
    "number of module-internal users of the callee, +1 if the callee is "      \
    "exposed externally")

// clang-format off
enum class FeatureIndex : size_t {
#define POPULATE_INDICES(DTYPE, SHAPE, NAME, COMMENT) NAME,
// InlineCost features - these must come first
  INLINE_COST_FEATURE_ITERATOR(POPULATE_INDICES)

// Non-cost features
  INLINE_FEATURE_ITERATOR(POPULATE_INDICES)
#undef POPULATE_INDICES

  NumberOfFeatures
};
// clang-format on

constexpr FeatureIndex
inlineCostFeatureToMlFeature(InlineCostFeatureIndex Feature) {
  return static_cast<FeatureIndex>(static_cast<size_t>(Feature));
}

constexpr size_t NumberOfFeatures =
    static_cast<size_t>(FeatureIndex::NumberOfFeatures);

extern const std::vector<TensorSpec> FeatureMap;

extern const char *const DecisionName;
extern const TensorSpec InlineDecisionSpec;
extern const char *const DefaultDecisionName;
extern const TensorSpec DefaultDecisionSpec;
extern const char *const RewardName;

using InlineFeatures = std::vector<int64_t>;

} // namespace llvm
#endif // LLVM_ANALYSIS_INLINEMODELFEATUREMAPS_H
