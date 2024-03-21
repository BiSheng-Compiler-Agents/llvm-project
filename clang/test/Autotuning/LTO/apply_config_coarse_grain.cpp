// REQUIRES: asserts

// RUN: rm -rf %t.data_dir
// RUN: export AUTOTUNE_DATADIR=%t.data_dir
// RUN: mkdir $AUTOTUNE_DATADIR

// RUN: sed  's#\[module\]#%S/Inputs/src/input.c#g' \
// RUN:     %S/Inputs/datadir/corse_grain_config.yaml > %t.data_dir/config.yaml

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune %S/Inputs/src/output.c \
// RUN:     -o %t.output.o -mllvm -debug-only=loop-unroll -mllvm -unroll-count=0 \
// RUN:     2>&1 | FileCheck %s --check-prefix=COARSEGRAIN1

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune %S/Inputs/src/input.c \
// RUN:     -o %t.input.o -mllvm -debug-only=loop-unroll 2>&1 | \
// RUN:     FileCheck %s --check-prefix=COARSEGRAIN2

// RUN: %clang -O3 -c -flto=full -fuse-ld=lld -fautotune %S/Inputs/src/test.c \
// RUN:     -o %t.test.o -mllvm -debug-only=loop-unroll -mllvm -unroll-count=0 \
// RUN:     2>&1 | FileCheck %s --check-prefix=COARSEGRAIN1

// RUN: sed 's#\[module\]#a.out#g' %S/Inputs/datadir/corse_grain_config.yaml \
// RUN:     > %t.data_dir/config.yaml

// RUN: env AUTOTUNE_PROJECT_DIR=$(echo -n %T | sed 's\Output\\') \
// RUN:     %clang -O3 -fautotune %t.output.o %t.input.o %t.test.o \
// RUN:     -flto=full -fuse-ld=lld -Wl,-mllvm,-debug-only=loop-unroll 2>&1 |\
// RUN:     FileCheck %s --check-prefix=COARSEGRAIN3

// COARSEGRAIN1-NOT: UNROLLING loop

// COARSEGRAIN2: Loop Unroll: F[input_data] Loop [[NAME:%for.body]]
// COARSEGRAIN2: UNROLLING loop [[NAME]]

// COARSEGRAIN3: Loop Unroll: F{{\[}}[[FUNCNAME:main]]{{\]}} Loop %
// COARSEGRAIN3: UNROLLING loop %
// COARSEGRAIN3: Loop Unroll: F{{\[}}[[FUNCNAME]]{{\]}} Loop %
// COARSEGRAIN3: UNROLLING loop %
// COARSEGRAIN3: Loop Unroll: F{{\[}}[[FUNCNAME]]{{\]}} Loop %
// COARSEGRAIN3: UNROLLING loop %
// COARSEGRAIN3-NOT: UNROLLING loop
