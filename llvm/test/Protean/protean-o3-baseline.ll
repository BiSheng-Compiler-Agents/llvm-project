; REQUIRES: have_tf_aot
; The first SA iteration must run the full O3 optimization pipeline and return
; its output when no other recipes are evaluated.
; RUN: protean -O3 -o %t.o3.bc %s
; RUN: protean -protean -passes=verify -cost-type=InstCount -module-level-ipc=true \
; RUN:   -max-iterations=1 -debug-pass-manager -o %t.sa.bc %s 2>&1 | FileCheck %s
; RUN: llvm-diff %t.o3.bc %t.sa.bc
; RUN: diff %t.sa-O3.bc %t.sa.bc

; SA children collect features twice: once inside the pipeline and once more
; after the output has been written.
; CHECK: Running pass: LoopVectorizePass on f
; CHECK: Running pass: CollectFeaturesPass on
; CHECK: Running pass: BitcodeWriterPass on
; CHECK-NEXT: Running pass: CollectFeaturesPass on

define i32 @f(i32 %x) {
  %sum = add i32 %x, 0
  ret i32 %sum
}
