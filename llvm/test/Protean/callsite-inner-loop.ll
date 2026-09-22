; RUN: opt -passes=protean-collect-features -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; A call in a nested innermost loop must be counted.
; CHECK: is_in_inner_loop = 1

define void @caller(i1 %again) {
entry:
  br label %outer

outer:
  br label %inner

inner:
  call void @callee()
  br i1 %again, label %inner, label %outer.latch

outer.latch:
  br i1 %again, label %outer, label %exit

exit:
  ret void
}

define void @callee() {
  ret void
}
