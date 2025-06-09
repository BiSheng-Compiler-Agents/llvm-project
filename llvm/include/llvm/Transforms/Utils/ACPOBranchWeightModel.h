// ===- ACPOBranchWeightModel.h - ACPO Branch weight model ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
//
//===------------------------------------------------------------------------===//
#if defined(ENABLE_ACPO)
#ifndef LLVM_TRANSFORM_UTILS_ACPOBRANCHWEIGHTMODEL_H
#define LLVM_TRANSFORM_UTILS_ACPOBRANCHWEIGHTMODEL_H

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class ACPOBranchWeightModelPass
    : public PassInfoMixin<ACPOBranchWeightModelPass> {
public:
  PreservedAnalyses run(Module &m, ModuleAnalysisManager &MAM);

private:
  bool applyBranchWeightUsingACPOModel(Module &M, ModuleAnalysisManager &MAM);
};
}

#endif //LLVM_TRANSFORM_UTILS_ACPOBRANCHWEIGHTMODEL_H
#endif // ENABLE_ACPO
