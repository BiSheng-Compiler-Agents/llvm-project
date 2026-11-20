; RUN: rm -f %t1 %t2 %tp
; RUN: protean --enable-protean-feature-dump --protean-dump-file=%tp --ir2vec-dump-file=%t1 < %s
; RUN: protean --enable-protean-feature-dump --protean-dump-file=%tp --ir2vec-dump-file=%t2 < %s
; RUN: cat %t1 | tr ',' '\n' | wc -l | FileCheck %s
; RUN: cat %t2 | tr ',' '\n' | wc -l | FileCheck %s

; CHECK: 300

define void @main() {
  ret void
}
