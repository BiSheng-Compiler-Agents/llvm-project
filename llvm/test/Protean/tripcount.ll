; RUN: split-file %s %t
; RUN: opt -passes=protean-collect-features -S %t/known.ll | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --check-prefix=KNOWN --match-full-lines
; RUN: opt -passes=protean-collect-features -S %t/unknown.ll | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --check-prefix=UNKNOWN --match-full-lines
; RUN: opt -passes=protean-collect-features -S %t/infinite.ll | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --check-prefix=INFINITE --match-full-lines

; Distinct constant exits contribute to both extrema.
; KNOWN-DAG: MinKnownTripCount = 4
; KNOWN-DAG: MaxKnownTripCount = 8
; KNOWN-DAG: IsFixedTripCount = 1

; An unknown exit is ignored by the extrema but clears IsFixedTripCount.
; UNKNOWN-DAG: MinKnownTripCount = 8
; UNKNOWN-DAG: MaxKnownTripCount = 8
; UNKNOWN-DAG: IsFixedTripCount = 0

; Infinite loops don't have a fixed trip count.
; INFINITE-DAG: IsFixedTripCount = 0

;--- known.ll
define void @multiple_constant_exits() {
entry:
  br label %loop

loop:
  %iv = phi i32 [ 0, %entry ], [ %next, %latch ]
  %next = add nuw nsw i32 %iv, 1
  %continue.first = icmp ult i32 %next, 4
  br i1 %continue.first, label %latch, label %exit

latch:
  %continue.second = icmp ult i32 %next, 8
  br i1 %continue.second, label %loop, label %exit

exit:
  ret void
}

;--- unknown.ll
define void @unknown_early_exit(i1 %stop) {
entry:
  br label %loop

loop:
  %iv = phi i32 [ 0, %entry ], [ %next, %latch ]
  br i1 %stop, label %exit, label %latch

latch:
  %next = add nuw nsw i32 %iv, 1
  %continue = icmp ult i32 %next, 8
  br i1 %continue, label %loop, label %exit

exit:
  ret void
}

;--- infinite.ll
define void @infinite_loop() {
entry:
  br label %loop

loop:
  br label %loop
}
