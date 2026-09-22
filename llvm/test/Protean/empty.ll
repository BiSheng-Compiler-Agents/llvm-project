; RUN: opt -passes=protean-collect-features -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; CHECK-DAG: average_calls_per_function = 0
; CHECK-DAG: average_bb_per_function = 0
; CHECK-DAG: average_instructions_per_function = 0
; CHECK-DAG: average_load_instructions_per_function = 0
; CHECK-DAG: average_store_instructions_per_function = 0

declare void @empty()
