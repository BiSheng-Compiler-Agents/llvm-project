#if defined(ENABLE_AUTOTUNER)
// ===------------ llvm/AutoTuner/AutoTuningRemarkStreamer.h --------------===//
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

#ifndef LLVM_AUTOTUNER_AUTOTUNINGREMARKSTREAMER_H
#define LLVM_AUTOTUNER_AUTOTUNINGREMARKSTREAMER_H

#include "llvm/AutoTuner/AutoTuning.h"
#include "llvm/Remarks/Remark.h"
#include "llvm/Remarks/RemarkStreamer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ToolOutputFile.h"
#include <memory>
#include <string>

namespace llvm {
/// Streamer for AutoTuner remarks which has logic for dealing with CodeRegions.
class AutoTuningRemarkStreamer {
  remarks::RemarkStreamer &RS;
  /// Convert CodeRegion into remark objects.
  remarks::Remark toRemark(const autotuning::CodeRegion &CR);

public:
  AutoTuningRemarkStreamer(remarks::RemarkStreamer &RS) : RS(RS) {}
  /// Emit a CodeRegion through the streamer.
  void emit(const autotuning::CodeRegion &CR);
  /// Set a pass filter based on a regex \p Filter.
  /// Returns an error if the regex is invalid.
  Error setFilter(StringRef Filter);
};
} // end namespace llvm

#endif // LLVM_AUTOTUNER_AUTOTUNINGREMARKSTREAMER_H
#endif
