#if defined(ENABLE_AUTOTUNER)
//===- llvm/AutoTuner/AutoTuningRemarkManager.h - Remark Manager ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the main interface for inputting and outputting
// remarks for AutoTuning.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_AUTOTUNINGREMARKMANAGER_H
#define LLVM_AUTOTUNINGREMARKMANAGER_H

#include "llvm/AutoTuner/AutoTuning.h"
#include "llvm/Remarks/RemarkStreamer.h"
#include "llvm/Support/Error.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace autotuning {
class AutoTuningRemarkManager {
public:
  /// Read a list of parameters from input file.
  /// Return true on success and false on failure.
  static llvm::Error read(autotuning::AutoTuningEngine &E,
                          const std::string &InputName,
                          const std::string &RemarksFormat);

  /// Dump a list of CodeRegions as tuning opportunities into a file.
  /// Return true on success and false on failure.
  static llvm::Error dump(const autotuning::AutoTuningEngine &E,
                          const std::string &DirPath,
                          const std::string &RemarksFormat,
                          const std::string &RemarksPasses);
};
} // namespace autotuning
#endif // LLVM_AUTOTUNINGREMARKMANAGER_H
#endif
