//===- ACPOBWModel.cpp - ACPO Branch weight inference ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
//
//==-----------------------------------------------------------------------===//
//
// This file implements the interface between ACPO and ML-guided optimizations.
// It delegates decision making to inference with a pre-trained model.
//
//==-----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/ACPOBWModel.h"

using namespace llvm;

#define DEBUG_TYPE "acpo-bw-model"

ACPOBWModel::ACPOBWModel(LLVMContext *Context, OptimizationRemarkEmitter *ORE)
    : ACPOModel(ORE, true) {
  setContextPtr(Context);
  // Python support is turned off
  setMLIF(createPersistentCompiledMLIF());
}

ACPOBWModel::~ACPOBWModel() {}

void ACPOBWModel::setMLCustomFeatures(
    std::vector<std::pair<std::string, std::string>> FeatureValues) {
  CustomFeatureValues = FeatureValues;
}

std::unique_ptr<ACPOAdvice> ACPOBWModel::getAdviceML() {
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  // Generate result.
  std::unique_ptr<ACPOAdvice> Advice = std::make_unique<ACPOAdvice>();
  assert(MLIR != nullptr);
  if (!MLIF->loadModel("") ||
      !MLIF->initializeFeatures("BW", CustomFeatureValues)) {
    outs() << "Model not loaded or features not initialized. "
           << "Did you export BISHENG_ACPO_DIR to $LLVM_DIR/acpo ?\n"
           << "Falling back to default advisor. \n";
    return nullptr;
  }
  bool ModelRunOK = MLIF->runModel("BW");
  assert(ModelRunOK);
  BranchWeight = MLIF->getModelResultI("BW-BranchWeight");
  assert(getContextPtr() != nullptr);
  Advice->addField("BW-BranchWeight", ConstantInt::get(Type::getInt64Ty(*(getContextPtr())),
                                                     (int64_t)BranchWeight));

  return Advice;
}

std::unique_ptr<ACPOAdvice> ACPOBWModel::getAdviceNoML() { return nullptr; }
#endif // ENABLE_ACPO
