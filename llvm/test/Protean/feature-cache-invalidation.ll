; RUN: rm -f %t.csv
; RUN: opt -passes='protean-collect-features,function(instcombine),protean-collect-features' \
; RUN:   -enable-protean-feature-dump -protean-dump-file=%t.csv -disable-output %s
; RUN: %python -c "import csv, sys; print(*(r['callee_InitialSize'] for r in csv.DictReader(open(sys.argv[1])) if r['Module|Function|Callee|Caller|Loop'].split('|')[2] == 'callee'), sep='\n')" %t.csv | FileCheck %s

; Instcombine removes the identity add while preserving the TTI analysis.
; The second collection must recompute the callee's size rather than reuse
; the value cached by the first collection. Use the dump because existing
; feature module flags are retained from the first collection.
; CHECK: 2
; CHECK-NEXT: 1

define i32 @caller(i32 %x) {
  %result = call i32 @callee(i32 %x)
  ret i32 %result
}

define i32 @callee(i32 %x) {
  %identity = add i32 %x, 0
  ret i32 %identity
}
