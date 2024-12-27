#ifndef IR2SCORE_H
#define IR2SCORE_H
//===- IR2ScoreModel.h - AI-Enabled Continuous Program Optimization
//---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2022-2023. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IR2SCOREMODEL_H
#define LLVM_ANALYSIS_IR2SCOREMODEL_H

#include "llvm/Analysis/ACPOModel.h"
#include "llvm/IR/LLVMContext.h"

namespace llvm {

class ACPOmodel;

class IR2ScoreModel : public ACPOModel {
public:
  bool UseProteanCollect = false;
  IR2ScoreModel(LLVMContext *Context, bool UseAOT = true,
                OptimizationRemarkEmitter *ORE = NULL, bool UseML = true);

  ~IR2ScoreModel();

  void setMLCustomFeatures(
      std::vector<std::pair<std::string, std::string>> FeatureValues);

  void sendCustomFeatures() override;

  void setContext(LLVMContext *Context);

  void setProteanCollect(bool ProteanCollect);

  size_t getModelFeaturesSize();

protected:
  // Interface to run the MLInference/default advisor and get advice from the
  // model/default advisor
  virtual std::unique_ptr<ACPOAdvice> getAdviceML() override;

  virtual std::unique_ptr<ACPOAdvice> getAdviceNoML() override;

private:
  std::vector<std::pair<std::string, std::string>> CustomFeatureValues;
  bool UseAOTModel;
  std::vector<float> YScaling = {100.0, 0.0};
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_IR2SCOREMODEL_H
#endif // IR2SCORE_H
