//===- ACPOBWModel.h - ACPO Branch weight inference -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
//
//==-----------------------------------------------------------------------===//
#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_ACPOBWMODEL_H
#define LLVM_ANALYSIS_ACPOBWMODEL_H

#include "llvm/Analysis/ACPOModel.h"

namespace llvm {

class ACPOBWModel : public ACPOModel {
public:
  ACPOBWModel(LLVMContext *Context, OptimizationRemarkEmitter *OER);
  ~ACPOBWModel();
  void setMLCustomFeatures(
      std::vector<std::pair<std::string, std::string>> FeatureValues);

protected:
  // Interface to run the MLInference/default advisor and get advice from
  // model/default advisor
  virtual std::unique_ptr<ACPOAdvice> getAdviceML() override;

  virtual std::unique_ptr<ACPOAdvice> getAdviceNoML() override;

private:
  std::vector<std::pair<std::string, std::string>> CustomFeatureValues;
  uint64_t BranchWeight;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_ACPOBWMODEL_H
#endif // ENABLE_ACPO
