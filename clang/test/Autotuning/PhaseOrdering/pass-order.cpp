// Disable auto-tuning
// RUN: %clang %s -S -mllvm -debug-pass=Arguments 2>&1 >/dev/null | \
// RUN:    FileCheck %s -check-prefix=DISABLE

// One Pass
// RUN: rm %t.onepass-debug-pass.yaml -rf
// RUN: sed 's#\[filename\]#%s#g; s#\[pass\]#\[loop-extract\]#g' \
// RUN:    %S/Inputs/template.yaml > %t.onepass-debug-pass.yaml
// RUN: %clang %s -S -mllvm -auto-tuning-input=%t.onepass-debug-pass.yaml \
// RUN:    -mllvm -print-after-all 2>&1 >/dev/null | \
// RUN:    FileCheck %s -check-prefix=ONEPASS

// Two passes (A->B):
// RUN: rm %t.twopass-ab-debug-pass.yaml -rf
// RUN: sed 's#\[filename\]#%s#g; s#\[pass\]#\[loop-extract,strip\]#g' \
// RUN:    %S/Inputs/template.yaml  > %t.twopass-ab-debug-pass.yaml
// RUN: %clang %s -S -mllvm -auto-tuning-input=%t.twopass-ab-debug-pass.yaml \
// RUN:    -mllvm -print-after-all 2>&1 >/dev/null | \
// RUN:    FileCheck %s -check-prefix=TWOPASS_AB

// Two passes (B->A):
// RUN: rm %t.twopass-ba-debug-pass.yaml -rf
// RUN: sed 's#\[filename\]#%s#g; s#\[pass\]#\[strip,loop-extract\]#g' \
// RUN:    %S/Inputs/template.yaml  > %t.twopass-ba-debug-pass.yaml
// RUN: %clang %s -S -mllvm -auto-tuning-input=%t.twopass-ba-debug-pass.yaml \
// RUN:    -mllvm -print-after-all 2>&1 >/dev/null | \
// RUN:    FileCheck %s -check-prefix=TWOPASS_BA
// UNSUPPORTED: windows

// a simple cpp file
int main() {
  int i = 8;
  for (; i < 20;) {
    int a = i - 5;
    i = i + 2;
  }
}

// DISABLE: Pass Arguments:
// DISABLE-NOT: -loop-extract

// ONEPASS: *** IR Dump After LoopExtractorPass

// TWOPASS_AB: *** IR Dump After LoopExtractorPass
// TWOPASS_AB: *** IR Dump After StripSymbolsPass

// TWOPASS_BA: *** IR Dump After StripSymbolsPass
// TWOPASS_BA: *** IR Dump After LoopExtractorPass
