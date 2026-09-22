; RUN: opt -passes=protean-collect-features -preserve-input-debuginfo-format -S %s | \
; RUN:   %python %S/Inputs/extract-protean-features.py | FileCheck %s --match-full-lines

; The debug label must not affect either loop instruction total or load fraction.

; CHECK-DAG: NumLoadInstPerLoopNest = 2
; CHECK-DAG: NumStoreInstPerLoopNest = 2
; CHECK-DAG: TotLoopNestInstCount = 7
; CHECK-DAG: AvgNumLoadInstPerLoopNest = 0.285714298

; CHECK-DAG: NumLoadInstPerLoop = 1
; CHECK-DAG: NumStoreInstPerLoop = 1
; CHECK-DAG: TotLoopInstCount = 3
; CHECK-DAG: AvgNumLoadInstPerLoop = 0.333333343
; CHECK-DAG: TotBlocksPerLoop = 1

define void @sibling_loops(ptr %p, i1 %again) !dbg !4 {
entry:
  br label %outer.header

outer.header:
  %outer.value = load i32, ptr %p
  call void @llvm.dbg.label(metadata !6), !dbg !7
  store i32 %outer.value, ptr %p
  br label %inner.load

inner.load:
  %inner.value = load i32, ptr %p
  br i1 %again, label %inner.load, label %inner.store

inner.store:
  store i32 0, ptr %p
  br i1 %again, label %inner.store, label %outer.header
}

declare void @llvm.dbg.label(metadata)

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "test", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "loop-inst-features.c", directory: "/")
!2 = !{i32 2, !"Debug Info Version", i32 3}
!3 = !DISubroutineType(types: !5)
!4 = distinct !DISubprogram(name: "sibling_loops", scope: !1, file: !1, line: 1, type: !3, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0)
!5 = !{}
!6 = !DILabel(scope: !4, name: "outer", file: !1, line: 2)
!7 = !DILocation(line: 2, column: 1, scope: !4)
