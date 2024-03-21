// RUN: rm -rf %t.data_dir
// RUN: export AUTOTUNE_DATADIR=%t.data_dir

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune-generate -o %t.output.o \
// RUN:     %S/Inputs/src/output.c
// RUN: FileCheck %s --input-file %t.data_dir/opp/output.c.yaml \
// RUN:     -check-prefix=FINEGRAIN

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune-generate -o %t.input.o \
// RUN:      %S/Inputs/src/input.c
// RUN: FileCheck %s --input-file %t.data_dir/opp/input.c.yaml \
// RUN:     -check-prefix=FINEGRAIN

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune-generate -o %t.test.o \
// RUN:     %S/Inputs/src/test.c
// RUN: FileCheck %s --input-file %t.data_dir/opp/test.c.yaml \
// RUN:     -check-prefix=FINEGRAIN

// RUN: %clang -O3 -flto=full -fuse-ld=lld -fautotune-generate %t.output.o \
// RUN:     %t.test.o %t.input.o
// RUN: FileCheck %s --input-file %t.data_dir/opp/a.out.yaml \
// RUN:     -check-prefix=FINEGRAIN-LTO

// RUN: rm -rf %t.data_dir
// RUN: export AUTOTUNE_DATADIR=%t.data_dir

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune-generate=LLVMParam \
// RUN:     %S/Inputs/src/output.c -o %t.output.o
// RUN: FileCheck %s --input-file %t.data_dir/opp/output.c.yaml \
// RUN:     -check-prefix=COARSEGRAIN

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune-generate=LLVMParam \
// RUN:     %S/Inputs/src/input.c -o %t.input.o
// RUN: FileCheck %s --input-file %t.data_dir/opp/input.c.yaml \
// RUN:     -check-prefix=COARSEGRAIN

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune-generate=LLVMParam \
// RUN:     %S/Inputs/src/test.c -o %t.test.o
// RUN: FileCheck %s --input-file %t.data_dir/opp/test.c.yaml \
// RUN:     -check-prefix=COARSEGRAIN

// RUN: %clang -O3 -flto=full -fuse-ld=lld -fautotune-generate=LLVMParam \
// RUN:     %t.test.o %t.input.o %t.output.o
// RUN: FileCheck %s --input-file %t.data_dir/opp/a.out.yaml \
// RUN:     -check-prefix=COARSEGRAIN

// FINEGRAIN: --- !AutoTuning
// FINEGRAIN: CodeRegionType:  {{callsite|loop}}

// FINEGRAIN-LTO: --- !AutoTuning
// FINEGRAIN-LTO: CodeRegionType:  callsite
// FINEGRAIN-LTO: --- !AutoTuning
// FINEGRAIN-LTO: CodeRegionType:  loop

// COARSEGRAIN: --- !AutoTuning
// COARSEGRAIN: CodeRegionType:  llvm-param
