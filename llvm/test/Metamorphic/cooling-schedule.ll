; RUN: protean -protean -passes=verify -debug-only=protean -o %t.bc -max-iterations=50 %s 2>&1 | FileCheck %s --check-prefix=GEOMETRIC
; RUN: protean -protean -passes=verify -debug-only=protean -o  %t.bc -max-iterations=50 -cooling=Linear %s 2>&1 | FileCheck %s --check-prefix=LINEAR
; RUN: protean -protean -passes=verify -debug-only=protean -o  %t.bc -max-iterations=100 %s 2>&1 | FileCheck %s --check-prefix=ITERATIONS

        %FunTy = type i32 (i32)

define void @invoke(ptr %x) {
        %foo = call i32 %x( i32 123 )           ; <i32> [#uses=0]
        ret void
}


; GEOMETRIC: Iteration 0
; GEOMETRIC: Iteration {{.*}}
; GEOMETRIC-NOT: Temperature: 0.000

; LINEAR: Iteration 0
; LINEAR: Iteration {{.*}}
; LINEAR-NOT: Temperature: 0.000

; ITERATIONS-NOT: Iteration 101
