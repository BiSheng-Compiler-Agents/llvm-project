; RUN: rm -f %t.csv
; RUN: opt -passes=protean-collect-features -enable-protean-feature-dump \
; RUN:   -protean-dump-file=%t.csv -S %s -o %t.ll
; RUN: %python %S/Inputs/extract-protean-features.py %t.ll | FileCheck %s --match-full-lines
; RUN: FileCheck %s --check-prefix=CSV < %t.csv
; RUN: %python -c "import csv, sys; print(*(','.join(r[k] for k in ('caller_basic_block_count', 'callee_basic_block_count', 'nr_ctant_params', 'function_count', 'NumLoadInstPerLoopNest')) for r in csv.DictReader(open(sys.argv[1]))), sep='\n')" %t.csv | FileCheck %s --check-prefix=ROWS --match-full-lines
; RUN: %python -c "import csv, math, runpy, sys; rows = list(csv.DictReader(open(sys.argv[1]))); \
; RUN:   extract = runpy.run_path(sys.argv[3])['extract_features']; \
; RUN:   mismatches = [k for k, v in extract(open(sys.argv[2]).read()) if not math.isclose(sum(float(r[k]) for r in rows), float(v), rel_tol=1e-6, abs_tol=1e-6)]; \
; RUN:   assert not mismatches, mismatches" %t.csv %t.ll %S/Inputs/extract-protean-features.py

; Count each module or function or loop only once.
; CHECK-DAG: caller_basic_block_count = 2
; CHECK-DAG: callee_basic_block_count = 2
; CHECK-DAG: nr_ctant_params = 4
; CHECK-DAG: function_count = 4
; CHECK-DAG: total_bb_count = 7

; Two distinct top-level loops: each contribute one load and two instructions.
; Note that these are aggregated values over both loops.
; CHECK-DAG: NumLoadInstPerLoopNest = 2
; CHECK-DAG: TotLoopNestInstCount = 4
; CHECK-DAG: TotBlocksPerLoop = 2

; All four callsite rows remain in the CSV, including repeated calls.
; CSV: |a|b|a|,
; CSV-NEXT: {{.*}}|a|b|a|,
; CSV-NEXT: {{.*}}|a|c|a|,
; CSV-NEXT: {{.*}}|b|c|b|,

; Module features appear only once, in the module row.
; Callers and callees contribute features only on their first appearance in that
; role. Each callsite and loop contribute fully, as they cannot repeat.
; ROWS:      0,0,0,4,0
; ROWS-NEXT: 1,1,1,0,0
; ROWS-NEXT: 0,0,1,0,0
; ROWS-NEXT: 0,1,1,0,0
; ROWS-NEXT: 1,0,1,0,0
; ROWS-NEXT: 0,0,0,0,1
; ROWS-NEXT: 0,0,0,0,1
; ROWS-NOT: {{.}}

define void @a() {
  call void @b(i32 1)
  call void @b(i32 2)
  call void @c(i32 3)
  ret void
}

define void @b(i32 %x) {
  call void @c(i32 4)
  ret void
}

define void @c(i32 %x) {
  ret void
}

define void @uncalled(i1 %again, ptr %p) {
entry:
  br label %first
first:
  %x = load i32, ptr %p
  br i1 %again, label %first, label %second
second:
  %y = load i32, ptr %p
  br i1 %again, label %second, label %exit
exit:
  ret void
}
