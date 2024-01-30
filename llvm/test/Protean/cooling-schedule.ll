; RUN: protean -protean -passes=verify -debug-only=protean -o %t.bc %s 2>&1 | FileCheck %s --check-prefix=GEOMETRIC
; RUN: protean -protean -passes=verify -debug-only=protean -o %t.bc -cooling=linear %s 2>&1 | FileCheck %s --check-prefix=LINEAR
; RUN: protean -protean -passes=verify -debug-only=protean -o %t.bc -max-iterations=100 %s 2>&1 | FileCheck %s --check-prefix=ITERATIONS

%FunTy = type i32 (i32)

define void @invoke(ptr %x) {
  %foo = call i32 %x(i32 123)           ; <i32> [#uses=0]
  ret void
}

; Checks that cools down in exactly 50 iterations

; GEOMETRIC-COUNT-50: Iteration
; GEOMETRIC-NOT: Iteration 51

; LINEAR-COUNT-50: Iteration
; LINEAR-NOT: Iteration 51

; ITERATIONS-COUNT-100: Iteration
; ITERATIONS-NOT: Iteration 101

; CHECK: Temperature:1.000000e-01
