add_llvm_component_library(LLVMAutoTuner
  AutoTuning.cpp
  AutoTuningRemarkManager.cpp
  AutoTuningRemarkStreamer.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/AutoTuner

  LINK_COMPONENTS
  Remarks
)