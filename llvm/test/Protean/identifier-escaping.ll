; RUN: cp %s '%t,%%2C.ll'
; RUN: rm -f %t.csv
; RUN: opt -passes=protean-collect-features -enable-protean-feature-dump \
; RUN:   -protean-dump-file=%t.csv -disable-output '%t,%%2C.ll'
; RUN: FileCheck %s < %t.csv

; CHECK: Module|Function|Callee|Caller|Loop,
; CHECK-NEXT: [[MODULE:[^,|]+%2C%252C.ll]]||||,
; CHECK-NEXT: [[MODULE]]|[[CALLER:%2D%2C%7C%252C%22%0D%0A]]|f%2C|[[CALLER]]|,
; CHECK-NEXT: [[MODULE]]|[[CALLER]]|||loop%2C,

define void @"f,"() {
  ret void
}

define void @"-,|%2C\22\0D\0A"() {
entry:
  call void @"f,"()
  br label %"loop,"

"loop,":
  br i1 false, label %"loop,", label %exit

exit:
  ret void
}
