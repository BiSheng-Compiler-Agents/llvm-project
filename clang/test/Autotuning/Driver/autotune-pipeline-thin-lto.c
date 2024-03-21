// REQUIRES: asserts

// RUN: rm -rf %t.data_dir
// RUN: export AUTOTUNE_DATADIR=%t.data_dir
// RUN: mkdir $AUTOTUNE_DATADIR

// RUN: sed  's#\[type\]#loop#g' %S/Inputs/template.yaml > \
// RUN:     %t.data_dir/config.yaml
// RUN: %clang -O3 %s -flto=thin -fautotune -mllvm -debug-only=autotuning \
// RUN:     -fuse-ld=lld -Wl,-mllvm,-debug-only=autotuning 2>&1 | \
// RUN:     FileCheck %s --check-prefix=AUTOTUNE-CR-LOOP

// RUN: sed  's#\[type\]#llvm-param#g' %S/Inputs/template.yaml > \
// RUN:     %t.data_dir/config.yaml
// RUN: %clang -O3 %s -flto=thin -fautotune -mllvm -debug-only=autotuning \
// RUN:     -fuse-ld=lld -Wl,-mllvm,-debug-only=autotuning 2>&1 | \
// RUN:     FileCheck %s --check-prefix=AUTOTUNE-CR-PARAM

#include <assert.h>
#include <stdlib.h>

int main() {
  int cs = 128370 * 1024 / sizeof(double);
  double *flush = (double *)calloc(cs, sizeof(double));
  int i;
  double tmp = 0.0;
  for (i = 0; i < cs; i++)
    tmp += flush[i];
  assert(tmp <= 10.0);
  free(flush);
  return tmp;
}

// AUTOTUNE-CR-LOOP-NOT: AutoTuner does not support tuning of {{.*}} thinLTO
// AUTOTUNE-CP-LOOP-NEXT: AutoTuningEngine is initialized.
// AUTOTUNE-CR-LOOP: AutoTuner does not support tuning of {{.*}} thinLTO
// AUTOTUNE-CP-LOOP-NEXT: AutoTuningEngine is initialized.

// AUTOTUNE-CR-PARAM-NOT: AutoTuner does not support tuning of {{.*}} thinLTO
// AUTOTUNE-CP-PARAM-NEXT: AutoTuningEngine is initialized.
// AUTOTUNE-CR-PARAM-NOT: AutoTuner does not support tuning of {{.*}} thinLTO
// AUTOTUNE-CP-PARAM-NEXT: AutoTuningEngine is initialized.
