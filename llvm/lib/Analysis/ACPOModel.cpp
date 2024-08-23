//===- ACPOModel.cpp - AI-Enabled Continuous Program Optimization ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the interface between ACPO and ML-guided optimizations.
// It delegates decision making to inference with a pre-trained model.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#include "llvm/Analysis/ACPOModel.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Support/Debug.h"
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "acpo"

ACPOAdvice::ACPOAdvice(std::unique_ptr<ACPOAdvice> &ResultFormat) {
  assert(ResultFormat != nullptr);
  for (auto &Entry : ResultFormat->getFieldMap()) {
    reserveField(Entry.first, Entry.second.T);
  }
}

void ACPOModel::prepareModelInput() {}

bool ACPOModel::runModel(std::unique_ptr<ACPOAdvice> &Result) { return true; }

void ACPOModel::addRequiredResultField(std::string name, Type::TypeID &ID) {
  ResultFormat->reserveField(name, ID);
}

std::unique_ptr<ACPOAdvice> ACPOModel::getAdvice() {
  if (ShouldUseML)
    return getAdviceML();
  else
    return getAdviceNoML();
}

std::unique_ptr<ACPOAdvice> ACPOModel::getAdviceML() {
  // This needs to be filled with a mechanism to invoke a model selected
  // using the ModelRunner.
  sendCustomFeatures();
  prepareModelInput();
  std::unique_ptr<ACPOAdvice> Result =
      std::make_unique<ACPOAdvice>(ResultFormat);

  if (runModel(Result))
    return Result;
  else
    return nullptr;
}

void ACPOModel::addFeature(int64_t ID, Constant *Val) {
  assert(CustomFeatureMap.find(ID) == CustomFeatureMap.end());
  CustomFeatureMap[ID] = Val;
}
#endif
