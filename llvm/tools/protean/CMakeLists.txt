set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  AggressiveInstCombine
  Analysis
  AsmParser
  BitWriter
  CFGuard
  CodeGen
  Core
  Coroutines
  Extensions
  IPO
  IRReader
  IRPrinter
  InstCombine
  Instrumentation
  MC
  ObjCARCOpts
  Remarks
  ScalarOpts
  Support
  Target
  TargetParser
  TransformUtils
  Vectorize
  Passes
  )

add_subdirectory(IRAnalysis)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_llvm_tool(protean
  NewPMDriver.cpp
  protean.cpp

  DEPENDS
  intrinsics_gen
  SUPPORT_PLUGINS
  )
export_executable_symbols_for_plugins(protean)

target_link_libraries(protean PRIVATE ProteanAnalysis)