; RUN: opt -passes=protean-collect-features -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; CHECK-DAG: callee_PtrCallee = 0
; CHECK-DAG: caller_PtrCallee = 1

define void @empty(ptr %p) {
ret void
}

define void @main() {
%p = alloca i32
call void @empty(ptr %p)
ret void
}
