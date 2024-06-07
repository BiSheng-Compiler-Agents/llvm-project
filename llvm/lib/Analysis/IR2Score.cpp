#if defined(BSPRIV_COMMON_ACPO)
//===- IR2ScoreModel.cpp - Protean Compiler                      //-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2022-2023. Huawei Technologies Co., Ltd. All rights reserved.
//
//===----------------------------------------------------------------------===//
//
// This file implements the interface between ACPO and ML-guided optimizations.
// It delegates decision making to inference with a pre-trained model.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/IR2Score.h"
#include "llvm/IR/LLVMContext.h"

using namespace llvm;

#define DEBUG_TYPE "ir2score"

IR2ScoreModel::IR2ScoreModel(LLVMContext *Context,
                             OptimizationRemarkEmitter *ORE, bool UseML)
    : ACPOModel(ORE, UseML) {
  setContextPtr(Context);
  setMLIF(createPersistentCompiledMLIF());
}

IR2ScoreModel::~IR2ScoreModel() {}
void IR2ScoreModel::setMLCustomFeatures(
    std::vector<std::pair<std::string, std::string>> FeatureValues) {
  CustomFeatureValues = FeatureValues;
}

void IR2ScoreModel::sendCustomFeatures() {
  // Get an ACPOMLInterface to communicate with the Python side
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  MLIF->initializeFeatures("IR2SCORE", CustomFeatureValues);
}

std::unique_ptr<ACPOAdvice> IR2ScoreModel::getAdviceML() {
  // get performance results from module metadata
  // Get module flags
  std::shared_ptr<ACPOMLInterface> MLIF = getMLIF();
  std::unique_ptr<ACPOAdvice> Score = std::make_unique<ACPOAdvice>();
  assert(MLIF != nullptr);
  if (!MLIF->loadModel("model-ir2score.acpo") ||
      !MLIF->initializeFeatures("IR2SCORE", CustomFeatureValues)) {
    outs() << "Model not loaded or features not initialized. "
           << "Did you export BISHENG_ACPO_DIR to $LLVM_DIR/acpo ?\n"
           << "Falling back to default advisor. \n";
    return NULL;
  }
  bool ModelRunOK = MLIF->runModel("IR2SCORE");
  assert(ModelRunOK);
  bool ShouldInline = MLIF->getModelResultI("IRSCORE");
  assert(getContextPtr() != nullptr);
  Score->addField("IRSCORE",
                  ConstantInt::get(Type::getInt64Ty(*(getContextPtr())),
                                   (int64_t)ShouldInline));
  return Score;
}

std::unique_ptr<ACPOAdvice> IR2ScoreModel::getAdviceNoML() {
  // Use the advisor used by default inlining
  return NULL;
}

#endif // BSPRIV_COMMON_ACPO
