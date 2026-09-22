; A compatible dump gets one header and another set of rows on append.
; RUN: rm -f %t.csv
; RUN: opt -passes=protean-collect-features -enable-protean-feature-dump -protean-dump-file=%t.csv -disable-output %s
; RUN: cp %t.csv %t.original
; RUN: opt -passes=protean-collect-features -enable-protean-feature-dump -protean-dump-file=%t.csv -disable-output %s
; RUN: tail -n +2 %t.original > %t.rows
; RUN: cat %t.original %t.rows > %t.expected
; RUN: diff %t.expected %t.csv
;
; Swapping columns keeps the column count but makes the schema incompatible.
; RUN: sed '1s/edge_count,node_count/node_count,edge_count/' %t.original > %t.csv
; RUN: cp %t.csv %t.before
; RUN: not opt -passes=protean-collect-features -enable-protean-feature-dump -protean-dump-file=%t.csv -disable-output %s 2>&1 | FileCheck %s
; RUN: diff %t.before %t.csv
;
; CHECK: error: Protean dump header mismatch in '{{.*}}'

define void @f() {
  ret void
}
