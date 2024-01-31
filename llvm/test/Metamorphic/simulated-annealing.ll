; REQUIRES: asserts
; RUN: protean -passes='verify' -o %t.out -protean < %s 2>&1 | FileCheck %s

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main() {
entry:
  %retval = alloca i32, align 4
  %x = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 5, ptr %x, align 4
  ret i32 0
}

; CHECK: Child exited with: 0
; CHECK: Running Recipe: [[PASSES:[a-zA-Z0-9,-<>]+]]
; CHECK-NEXT: Pipeline: verify,[[PASSES]]
; CHECK: Simulated Annealing finished running.