#if defined(ENABLE_AUTOTUNER)
// ===---------- llvm/AutoTuner/AutoTuningRemarkStreamer.cpp --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// Copyright (C) 2017-2022, Huawei Technologies Co., Ltd. All rights reserved.
//
// ===---------------------------------------------------------------------===//
//
// This file contains the implementation of the conversion between AutoTuner
// CodeRegions and serializable remarks::Remark objects.
//
// ===---------------------------------------------------------------------===//

#include "llvm/AutoTuner/AutoTuningRemarkStreamer.h"

using namespace llvm;

// autotuning::CodeRegion -> Remark
remarks::Remark
AutoTuningRemarkStreamer::toRemark(const autotuning::CodeRegion &CR) {
  remarks::Remark R; // The result.
  R.RemarkType = remarks::Type::AutoTuning;
  R.PassName = CR.getPassName();
  R.RemarkName = CR.getName();
  R.FunctionName = CR.getFuncName();
  const autotuning::SourceLocation &Location = CR.getSourceLoc();
  if (Location)
    R.Loc = remarks::RemarkLocation{Location.SourceFilePath,
                                    Location.SourceLine, Location.SourceColumn};
  R.CodeRegionType = CR.getTypeAsString();
  R.CodeRegionHash = CR.getHash();
  R.AutoTunerOptions = CR.getAutoTunerOptions();
  R.Invocation = CR.getInvocation();
  R.BaselineConfig = CR.getBaselineConfig();
  return R;
}

void AutoTuningRemarkStreamer::emit(const autotuning::CodeRegion &CR) {
  if (!RS.matchesFilter(CR.getPassName()))
    return;

  // First, convert the code region to a remark.
  remarks::Remark R = toRemark(CR);
  // Then, emit the remark through the serializer.
  RS.getSerializer().emit(R);
}

Error AutoTuningRemarkStreamer::setFilter(StringRef Filter) {
  return RS.setFilter(Filter);
}
#endif
