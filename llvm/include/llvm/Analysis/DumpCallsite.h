//===- DumpCallSite.h - Dump information about a callsite -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines the pass used to dump a callsite.
//
//===----------------------------------------------------------------------===//

#if defined(ENABLE_ACPO)
#ifndef LLVM_ANALYSIS_DUMPCALLSITE
#define LLVM_ANALYSIS_DUMPCALLSITE

#include "llvm/IR/PassManager.h"

namespace llvm {

class DumpCallsitePass : public PassInfoMixin<DumpCallsitePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};

} // namespace llvm

#endif
#endif // ENABLE_ACPO
