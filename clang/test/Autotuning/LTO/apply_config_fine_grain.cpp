// REQUIRES: asserts

// RUN: rm -rf %t.data_dir
// RUN: export AUTOTUNE_DATADIR=%t.data_dir
// RUN: mkdir $AUTOTUNE_DATADIR
// RUN: cp %S/Inputs/datadir/fine_grain_output.yaml %t.data_dir/config.yaml

// RUN: %clang -O3 -flto=full -fuse-ld=lld -fautotune %S/Inputs/src/output.c \
// RUN:     -c -mllvm -debug-only=loop-unroll -mllvm -debug-only=autotuning \
// RUN:     -mllvm -auto-tuning-code-region-matching-hash=false -o %t.output.o \
// RUN:     2>&1 -mllvm -print-before-all | \
// RUN:     FileCheck %s --check-prefix=FINEGRAIN1

// RUN: %clang -O3 -flto=full -fuse-ld=lld -fautotune %S/Inputs/src/input.c \
// RUN:     -c -mllvm -debug-only=loop-unroll -mllvm -debug-only=autotuning \
// RUN:     -mllvm -auto-tuning-code-region-matching-hash=false -o %t.input.o \
// RUN:     2>&1 | FileCheck %s --check-prefix=FINEGRAIN2

// RUN: %clang -O3 -flto=full -fuse-ld=lld -fautotune %S/Inputs/src/test.c \
// RUN:     -c -mllvm -debug-only=loop-unroll -mllvm -debug-only=autotuning \
// RUN:     -mllvm -auto-tuning-code-region-matching-hash=false -o %t.test.o \
// RUN:     2>&1 | FileCheck %s --check-prefix=FINEGRAIN2

// RUN: cp %S/Inputs/datadir/fine_grain_a.out.yaml %t.data_dir/config.yaml
// RUN: %clang -O3 -flto=full -fuse-ld=lld -fautotune %t.output.o %t.input.o \
// RUN:     %t.test.o -Wl,-mllvm,-auto-tuning-code-region-matching-hash=false \
// RUN:     -Wl,-mllvm,-debug-only=inline -Wl,-mllvm,-debug-only=loop-unroll \
// RUN:     -Wl,-mllvm,-debug-only=autotuning 2>&1 | \
// RUN:     FileCheck %s --check-prefix=FINEGRAIN3

// FINEGRAIN1: IR Dump Before LoopUnrollPass on [[FUNCNAME1:output_data]]
// FINEGRAIN1: Loop Unroll: F{{\[}}[[FUNCNAME1]]{{\]}} Loop %[[NAME:for.body]]
// FINEGRAIN1: UnrollCount is set for the CodeRegion
// FINEGRAIN1-NEXT: Name: [[NAME]]
// FINEGRAIN1-NEXT: FuncName: [[FUNCNAME1]]
// FINEGRAIN1: UNROLLING loop %[[NAME]]

// FINEGRAIN2-NOT: UnrollCount is set for the CodeRegion

// FINEGRAIN3: ForceInline is set for the CodeRegion
// FINEGRAIN3-NEXT: Name: [[CALLEE:input_data]]
// FINEGRAIN3: Inlining (cost=always): Force inlined by auto-tuning
// FINEGRAIN3-SAME: @[[CALLEE]]
// FINEGRAIN3: Loop Unroll: F{{\[}}[[FUNCNAME2:main]]{{\]}} Loop %
// FINEGRAIN3: UnrollCount is set for the CodeRegion
// FINEGRAIN3-NEXT: Name: label %
// FINEGRAIN3-NEXT: FuncName: [[FUNCNAME2]]
// FINEGRAIN3: UNROLLING loop %
// FINEGRAIN3: Loop Unroll: F{{\[}}[[FUNCNAME2]]{{\]}} Loop %
// FINEGRAIN3: UnrollCount is set for the CodeRegion
// FINEGRAIN3-NEXT: Name: label %
// FINEGRAIN3-NEXT: FuncName: [[FUNCNAME2]]
// FINEGRAIN3: UNROLLING loop %
// FINEGRAIN3: Loop Unroll: F{{\[}}[[FUNCNAME2]]{{\]}} Loop %
// FINEGRAIN3: UnrollCount is set for the CodeRegion
// FINEGRAIN3-NEXT: Name: label %
// FINEGRAIN3-NEXT: FuncName: [[FUNCNAME2]]
// FINEGRAIN3: UNROLLING loop %
