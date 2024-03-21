set(LLVM_TARGET_DEFINITIONS Options.td)

set(CLANG_DRIVER_OPTIONS)
if (LLVM_ENABLE_AUTOTUNER)
   list(APPEND CLANG_DRIVER_OPTIONS "-DENABLE_AUTOTUNER" )
endif()

tablegen(LLVM Options.inc ${CLANG_DRIVER_OPTIONS} -gen-opt-parser-defs  )

add_public_tablegen_target(ClangDriverOptions)
