// REQUIRES: asserts

// RUN: %clang -O3 -c -fautotune-generate=LLVMParam %S/Inputs/test1.c \
// RUN:     -mllvm -auto-tuning-compile-mode=CoarseGrain \
// RUN:     -mllvm -debug-only=autotuning-compile 2>&1 | \
// RUN: FileCheck %s -check-prefix=COARSEGRAIN
// RUN: rm -rf %S/Inputs/test1.ll

// RUN: %clang -O3 -c -fautotune-generate %S/Inputs/test1.c \
// RUN:     -mllvm -auto-tuning-compile-mode=FineGrain \
// RUN:     -mllvm -debug-only=autotuning-compile 2>&1 | \
// RUN: FileCheck %s -check-prefix=FINEGRAIN-NO-OPP
// RUN: rm -rf %S/Inputs/test1.ll

// RUN: %clang -O3 -c -fautotune-generate %S/Inputs/test2.c \
// RUN:     -mllvm -auto-tuning-compile-mode=FineGrain \
// RUN:     -mllvm -debug-only=autotuning-compile 2>&1 | \
// RUN: FileCheck %s -check-prefix=FINEGRAIN-INLINE
// RUN: rm -rf %S/Inputs/test2.ll

// RUN: %clang -O3 -c -fautotune-generate %S/Inputs/test3.c \
// RUN:     -mllvm -auto-tuning-compile-mode=FineGrain \
// RUN:     -mllvm -debug-only=autotuning-compile 2>&1 | \
// RUN: FileCheck %s -check-prefix=FINEGRAIN-UNROLL
// RUN: rm -rf %S/Inputs/test3.ll

// COARSEGRAIN: AutoTuningCompile: IR files writing before Pass: start.

// FINEGRAIN-NO-OPP: AutoTuningCompile: IR files writing before Pass: start.
// FINEGRAIN-NO-OPP-NEXT: AutoTuningCompile: IR files writing before
// FINEGRAIN-NO-OPP-SAME: Pass: inline.
// FINEGRAIN-NO-OPP: AutoTuningCompile: IR files writing before
// FINEGRAIN-NO-OPP-SAME: Pass: loop-vectorize.
// FINEGRAIN-NO-OPP-NEXT: AutoTuningCompile: IR files writing before Pass: end.

// FINEGRAIN-INLINE: AutoTuningCompile: IR files writing before Pass: start.
// FINEGRAIN-INLINE-NEXT: AutoTuningCompile: IR files writing before
// FINEGRAIN-INLINE-SAME: Pass: inline.

// FINEGRAIN-UNROLL: AutoTuningCompile: IR files writing before Pass: start.
// FINEGRAIN-UNROLL-NEXT: AutoTuningCompile: IR files writing before
// FINEGRAIN-UNROLL-SAME: Pass: inline.
// FINEGRAIN-UNROLL-NEXT: AutoTuningCompile: IR files writing before
// FINEGRAIN-UNROLL-SAME: Pass: loop-unroll.
