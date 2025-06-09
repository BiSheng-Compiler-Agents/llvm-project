#if defined(ENABLE_ACPO)

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_AI4CANALYSIS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_AI4CANALYSIS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include <cstdint>
#include <string>

namespace llvm {
class Function;
class Instruction;
class Module;

enum FuncFreqAttr { FFA_Normal, FFA_Cold, FFA_Hot };

class AI4CAnalysis : public PassInfoMixin<AI4CAnalysis> {
public:
  AI4CAnalysis();
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MMA);
};

}// end name sace llvm

#endif
#endif // Enable_ACPO