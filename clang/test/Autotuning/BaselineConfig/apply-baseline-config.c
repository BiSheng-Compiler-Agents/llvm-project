// Check that the baseline IR is the same as the IR from the first iteration of
// the autotuning process with --use-baseline-config enabled.
// REQUIRES: ! host-x86_64

// RUN: rm -f %t.baseline %t.firstIt_baseline %t.firstIt_random
// RUN: %clang -O3 %s -c -o %t.baseline
// RUN: strip %t.baseline
// RUN: %clang -O3 %s -c -o %t.firstIt_baseline -mllvm \
// RUN:     -auto-tuning-input=%S/Inputs/autotune_datadir/baseline-config.yaml \
// RUN:     -mllvm -auto-tuning-omit-metadata
// RUN: strip %t.firstIt_baseline
// RUN: cmp %t.firstIt_baseline %t.baseline

// RUN: %clang -O3 %s -c -o %t.firstIt_random -mllvm \
// RUN:     -auto-tuning-input=%S/Inputs/autotune_datadir/random-config.yaml \
// RUN:     -mllvm -auto-tuning-omit-metadata
// RUN: strip %t.firstIt_random
// RUN: not cmp %t.firstIt_random %t.baseline

#include <assert.h>
#include <stdlib.h>

void test() {
  int cs = 128370 * 1024 / sizeof(double);
  double *flush = (double *)calloc(cs, sizeof(double));
  int i;
  double tmp = 0.0;
  for (i = 0; i < cs; i++)
    tmp += flush[i];
  assert(tmp <= 10.0);
  free(flush);
}
