; RUN: rm -f %t.csv
; RUN: opt -passes=protean-collect-features -enable-protean-feature-dump \
; RUN:   -protean-dump-file=%t.csv -S %s -o %t.ll
; RUN: FileCheck %s --check-prefix=CSV --match-full-lines < %t.csv
; RUN: %python %S/Inputs/extract-protean-features.py %t.ll | FileCheck %s --check-prefix=METADATA --match-full-lines

; CSV: {{.*}},0.33333333333333331,{{.*}}
; METADATA: average_calls_per_function = 0.333333343
; METADATA: InitialIVValueInt = 1.07374182e+09

define void @caller() {
  call void @callee()
  ret void
}

define void @callee() {
  ret void
}

define void @third() {
entry:
  br label %loop

loop:
  %iv = phi i64 [ 1073741824, %entry ], [ %next, %loop ]
  %next = add nuw nsw i64 %iv, 1
  %continue = icmp ult i64 %next, 1073741832
  br i1 %continue, label %loop, label %exit

exit:
  ret void
}
