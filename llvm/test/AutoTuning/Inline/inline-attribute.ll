; RUN: rm %t.inline_opp -rf
; RUN: opt %s -S -passes='cgscc(inline)' -auto-tuning-opp=%t.inline_opp -auto-tuning-type-filter=CallSite --disable-output
; RUN: FileCheck %s --input-file %t.inline_opp/inline-attribute.ll.yaml -check-prefix=TEST-1
; RUN: FileCheck %s --input-file %t.inline_opp/inline-attribute.ll.yaml -check-prefix=TEST-2

; ModuleID = 'inline.c'
source_filename = "inline.c"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-linux-gnu"

; Function Attrs: noinline norecurse nounwind readnone uwtable willreturn
define dso_local i32 @mul(i32 %a) local_unnamed_addr #0 !dbg !10 {
entry:
  %mul = mul nsw i32 %a, %a, !dbg !12
  ret i32 %mul, !dbg !13
}

; Function Attrs: alwaysinline nounwind uwtable
define dso_local i32 @add(i32 %a) local_unnamed_addr #1 !dbg !14 {
entry:
  %add = shl nsw i32 %a, 1, !dbg !15
  ret i32 %add, !dbg !16
}

; Function Attrs: nounwind uwtable
define dso_local i32 @inc(i32 %a) local_unnamed_addr #2 !dbg !17 {
entry:
  %inc = add nsw i32 %a, 1, !dbg !18
  ret i32 %inc, !dbg !19
}

; Function Attrs: nounwind uwtable
define dso_local i32 @func(i32 %a) local_unnamed_addr #2 !dbg !20 {
entry:
  %call = call i32 @add(i32 %a), !dbg !21
  %call1 = call i32 @mul(i32 %a), !dbg !22
  %add = add nsw i32 %call, %call1, !dbg !23
  %call2 = call i32 @inc(i32 %a), !dbg !24
  %add3 = add nsw i32 %add, %call2, !dbg !25
  ret i32 %add3, !dbg !26
}

attributes #0 = { noinline norecurse nounwind readnone uwtable willreturn "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { alwaysinline nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6, !7, !8}
!llvm.ident = !{!9}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "Huawei Bisheng Compiler clang version 12.0.0 (729941c4adfa)", isOptimized: true, runtimeVersion: 0, emissionKind: LineTablesOnly, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "test.c", directory: "/home/m00629332/code/autoTuner/ir-hashing")
!2 = !{}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 1, !"wchar_size", i32 4}
!5 = !{i32 1, !"branch-target-enforcement", i32 0}
!6 = !{i32 1, !"sign-return-address", i32 0}
!7 = !{i32 1, !"sign-return-address-all", i32 0}
!8 = !{i32 1, !"sign-return-address-with-bkey", i32 0}
!9 = !{!"Huawei Bisheng Compiler clang version 12.0.0 (729941c4adfa)"}
!10 = distinct !DISubprogram(name: "mul", scope: !1, file: !1, line: 2, type: !11, scopeLine: 2, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!11 = !DISubroutineType(types: !2)
!12 = !DILocation(line: 3, column: 13, scope: !10)
!13 = !DILocation(line: 3, column: 5, scope: !10)
!14 = distinct !DISubprogram(name: "add", scope: !1, file: !1, line: 7, type: !11, scopeLine: 7, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!15 = !DILocation(line: 8, column: 13, scope: !14)
!16 = !DILocation(line: 8, column: 5, scope: !14)
!17 = distinct !DISubprogram(name: "inc", scope: !1, file: !1, line: 11, type: !11, scopeLine: 11, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!18 = !DILocation(line: 12, column: 12, scope: !17)
!19 = !DILocation(line: 12, column: 5, scope: !17)
!20 = distinct !DISubprogram(name: "func", scope: !1, file: !1, line: 15, type: !11, scopeLine: 15, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!21 = !DILocation(line: 16, column: 12, scope: !20)
!22 = !DILocation(line: 16, column: 19, scope: !20)
!23 = !DILocation(line: 16, column: 18, scope: !20)
!24 = !DILocation(line: 16, column: 26, scope: !20)
!25 = !DILocation(line: 16, column: 25, scope: !20)
!26 = !DILocation(line: 16, column: 5, scope: !20)

; TEST-1: Pass:            inline
; TEST-1-NOT: Pass:            inline

; TEST-2: Name:            inc
; TEST-2-NEXT: DebugLoc:        { File: test.c, Line: 16, Column: 26 }
; TEST-2-NEXT: Function:        func
; TEST-2-NEXT: CodeRegionType:  callsite
