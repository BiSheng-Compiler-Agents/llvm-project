; RUN: opt -passes=protean-collect-features -preserve-input-debuginfo-format -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; The debug label must not count as an instruction or make this function a node.
; CHECK-DAG: node_count = 0
; CHECK-DAG: total_instruction_count = 1
; CHECK-DAG: average_instructions_per_function = 1

; optnone preserves the debug label for the annealing instruction-cost test.
define void @unreachable_with_debug() noinline optnone !dbg !3 {
  call void @llvm.dbg.label(metadata !4), !dbg !5
  unreachable
}

declare void @llvm.dbg.label(metadata)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, emissionKind: FullDebug)
!1 = !DIFile(filename: "instruction-count-debug.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = distinct !DISubprogram(name: "unreachable_with_debug", spFlags: DISPFlagDefinition, unit: !0)
!4 = !DILabel(scope: !3, name: "entry", file: !1, line: 2)
!5 = !DILocation(line: 2, scope: !3)
