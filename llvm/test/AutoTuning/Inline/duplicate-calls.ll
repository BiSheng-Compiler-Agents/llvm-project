; RUN: rm %t.duplicate_calls -rf
; RUN: opt %s -S -passes='cgscc(inline)' -auto-tuning-opp=%t.duplicate_calls \
; RUN:     -auto-tuning-type-filter=CallSite --disable-output
; RUN: FileCheck %s --input-file %t.duplicate_calls/duplicate-calls.ll.yaml

; ModuleID = 'duplicate-calls.c'
source_filename = "duplicate-calls.c"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-linux-gnu"

; Function Attrs: nounwind uwtable
define dso_local void @bar(i32* nocapture %result, i32* %cfb, i32 %bytes) local_unnamed_addr #0 !dbg !10 {
entry:
  %call = tail call i32 @test(i32* %cfb, i32 %bytes) #1, !dbg !12
  store i32 %call, i32* %result, align 4, !dbg !13, !tbaa !14
  ret void, !dbg !18
}

declare dso_local i32 @test(i32*, i32) local_unnamed_addr #0

; Function Attrs: nounwind uwtable
define dso_local void @foo(i32* %cfb, i32* readnone %saved, i32* nocapture %result, i32 %bytes) local_unnamed_addr #0 !dbg !19 {
entry:
  %tobool.not = icmp eq i32* %cfb, null, !dbg !20
  br i1 %tobool.not, label %if.else, label %if.then.split, !dbg !20

if.then.split:                                    ; preds = %entry
  tail call void @bar(i32* %result, i32* nonnull %cfb, i32 %bytes), !dbg !21
  br label %return, !dbg !22

if.else:                                          ; preds = %entry
  %tobool1.not = icmp eq i32* %saved, null, !dbg !23
  br i1 %tobool1.not, label %if.else.split, label %return, !dbg !23

if.else.split:                                    ; preds = %if.else
  tail call void @bar(i32* %result, i32* null, i32 %bytes), !dbg !21
  br label %return, !dbg !23

return:                                           ; preds = %if.then.split, %if.else.split, %if.else
  ret void, !dbg !24
}

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6, !7, !8}
!llvm.ident = !{!9}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "Huawei BiSheng Compiler clang version 12.0.0 (clang-0d5d71fe6c22 flang-8b17fc131076)", isOptimized: true, runtimeVersion: 0, emissionKind: LineTablesOnly, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "duplicate-calls.c", directory: "/home/m00629332/benchmarks/cBench/source/security_pgp_d/src")
!2 = !{}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 1, !"wchar_size", i32 4}
!5 = !{i32 1, !"branch-target-enforcement", i32 0}
!6 = !{i32 1, !"sign-return-address", i32 0}
!7 = !{i32 1, !"sign-return-address-all", i32 0}
!8 = !{i32 1, !"sign-return-address-with-bkey", i32 0}
!9 = !{!"Huawei BiSheng Compiler clang version 12.0.0 (clang-0d5d71fe6c22 flang-8b17fc131076)"}
!10 = distinct !DISubprogram(name: "bar", scope: !1, file: !1, line: 7, type: !11, scopeLine: 8, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!11 = !DISubroutineType(types: !2)
!12 = !DILocation(line: 10, column: 16, scope: !10)
!13 = !DILocation(line: 10, column: 14, scope: !10)
!14 = !{!15, !15, i64 0}
!15 = !{!"int", !16, i64 0}
!16 = !{!"omnipotent char", !17, i64 0}
!17 = !{!"Simple C/C++ TBAA"}
!18 = !DILocation(line: 14, column: 1, scope: !10)
!19 = distinct !DISubprogram(name: "foo", scope: !1, file: !1, line: 17, type: !11, scopeLine: 18, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!20 = !DILocation(line: 22, column: 6, scope: !19)
!21 = !DILocation(line: 27, column: 2, scope: !19)
!22 = !DILocation(line: 23, column: 3, scope: !19)
!23 = !DILocation(line: 24, column: 11, scope: !19)
!24 = !DILocation(line: 28, column: 1, scope: !19)

; CHECK: --- !AutoTuning
; CHECK-NEXT: Pass:            inline
; CHECK-NEXT: Name:            bar-if.then.split
; CHECK-NEXT: DebugLoc:        { File: duplicate-calls.c, Line: 27, Column: 2 }
; CHECK-NEXT: Function:        foo
; CHECK-NEXT: CodeRegionType:  callsite
; CHECK-NEXT: CodeRegionHash:
; CHECK-NEXT: DynamicConfigs:  { ForceInline: [ 0, 1 ] }
; CHECK-NEXT: BaselineConfig:  { ForceInline: '1' }
; CHECK-NEXT: Invocation:      0
; CHECK-NEXT: ...
; CHECK-NEXT: --- !AutoTuning
; CHECK-NEXT: Pass:            inline
; CHECK-NEXT: Name:            bar-if.else.split
; CHECK-NEXT: DebugLoc:        { File: duplicate-calls.c, Line: 27, Column: 2 }
; CHECK-NEXT: Function:        foo
; CHECK-NEXT: CodeRegionType:  callsite
; CHECK-NEXT: CodeRegionHash:
; CHECK-NEXT: DynamicConfigs:  { ForceInline: [ 0, 1 ] }
; CHECK-NEXT: BaselineConfig:  { ForceInline: '1' }
; CHECK-NEXT: Invocation:      0
