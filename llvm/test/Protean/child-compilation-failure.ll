; REQUIRES: system-linux, asserts, ACPO_AOT
; RUN: not env BISHENG_ACPO_DIR=%S/../../../acpo protean -protean \
; RUN:   -cost-type=InstCount -max-iterations=1 -passes=protean-invalid-pass \
; RUN:   -debug-only=protean -o %t.bc %s 2>&1 | FileCheck %s

; An invalid pipeline makes the child exit unsuccessfully before producing IR.
; CHECK: unknown pass name 'protean-invalid-pass'
; CHECK: Recipe exited unexpected:
; CHECK-NOT: Could not calculate cost
; CHECK-NOT: Cost of new state:
; CHECK: Finished SA

define void @f() {
  ret void
}
