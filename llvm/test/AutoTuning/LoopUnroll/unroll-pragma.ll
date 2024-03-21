; RUN: rm %t.unroll_opp -rf
; RUN: opt  %s -S -auto-tuning-opp=%t.unroll_opp -auto-tuning-type-filter=Loop \
; RUN:     -passes='require<opt-remark-emit>,loop(loop-unroll-full)' --disable-output
; RUN: FileCheck %s --input-file %t.unroll_opp/unroll-pragma.ll.yaml -check-prefix=TEST-1
; RUN: FileCheck %s --input-file %t.unroll_opp/unroll-pragma.ll.yaml -check-prefix=TEST-2

; RUN: rm %t.unroll_opp -rf
; RUN: opt  %s -S -auto-tuning-opp=%t.unroll_opp -auto-tuning-type-filter=Loop \
; RUN:     -passes='require<opt-remark-emit>,function(loop-unroll)' --disable-output
; RUN: FileCheck %s --input-file %t.unroll_opp/unroll-pragma.ll.yaml -check-prefix=TEST-1
; RUN: FileCheck %s --input-file %t.unroll_opp/unroll-pragma.ll.yaml -check-prefix=TEST-2

; This function contains two loops. loop for.body is defined with a pragma
; unroll_count(4) and loop for.body9 is without a pragama. AutoTuner will only
; consider for.body9 as a tuning opportunity.

; ModuleID = 'loop-unroll.c'
source_filename = "loop-unroll.c"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-linux-gnu"

; Function Attrs: nofree norecurse nounwind uwtable
define dso_local void @loop(i32* noalias nocapture readonly %a, i32* noalias nocapture readonly %b, i32* noalias nocapture %c, i32* noalias nocapture %d, i32 %len) local_unnamed_addr #0 !dbg !10 {
entry:
  %cmp34 = icmp slt i32 0, %len, !dbg !12
  br i1 %cmp34, label %for.body.lr.ph, label %for.cond6.preheader, !dbg !13

for.body.lr.ph:                                   ; preds = %entry
  br label %for.body, !dbg !13

for.cond.for.cond6.preheader_crit_edge:           ; preds = %for.body
  br label %for.cond6.preheader, !dbg !13

for.cond6.preheader:                              ; preds = %for.cond.for.cond6.preheader_crit_edge, %entry
  %cmp732 = icmp slt i32 0, %len, !dbg !14
  br i1 %cmp732, label %for.body9.lr.ph, label %for.cond.cleanup8, !dbg !15

for.body9.lr.ph:                                  ; preds = %for.cond6.preheader
  br label %for.body9, !dbg !15

for.body:                                         ; preds = %for.body.lr.ph, %for.body
  %i.035 = phi i32 [ 0, %for.body.lr.ph ], [ %inc, %for.body ]
  %idxprom = zext i32 %i.035 to i64, !dbg !16
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %idxprom, !dbg !16
  %0 = load i32, i32* %arrayidx, align 4, !dbg !16, !tbaa !17
  %arrayidx2 = getelementptr inbounds i32, i32* %b, i64 %idxprom, !dbg !21
  %1 = load i32, i32* %arrayidx2, align 4, !dbg !21, !tbaa !17
  %add = add nsw i32 %1, %0, !dbg !22
  %arrayidx4 = getelementptr inbounds i32, i32* %c, i64 %idxprom, !dbg !23
  store i32 %add, i32* %arrayidx4, align 4, !dbg !24, !tbaa !17
  %inc = add nuw nsw i32 %i.035, 1, !dbg !25
  %cmp = icmp slt i32 %inc, %len, !dbg !12
  br i1 %cmp, label %for.body, label %for.cond.for.cond6.preheader_crit_edge, !dbg !13, !llvm.loop !26

for.cond6.for.cond.cleanup8_crit_edge:            ; preds = %for.body9
  br label %for.cond.cleanup8, !dbg !15

for.cond.cleanup8:                                ; preds = %for.cond6.for.cond.cleanup8_crit_edge, %for.cond6.preheader
  ret void, !dbg !30

for.body9:                                        ; preds = %for.body9.lr.ph, %for.body9
  %i5.033 = phi i32 [ 0, %for.body9.lr.ph ], [ %inc17, %for.body9 ]
  %idxprom10 = zext i32 %i5.033 to i64, !dbg !31
  %arrayidx11 = getelementptr inbounds i32, i32* %a, i64 %idxprom10, !dbg !31
  %2 = load i32, i32* %arrayidx11, align 4, !dbg !31, !tbaa !17
  %arrayidx13 = getelementptr inbounds i32, i32* %b, i64 %idxprom10, !dbg !32
  %3 = load i32, i32* %arrayidx13, align 4, !dbg !32, !tbaa !17
  %mul = mul nsw i32 %3, %2, !dbg !33
  %arrayidx15 = getelementptr inbounds i32, i32* %d, i64 %idxprom10, !dbg !34
  store i32 %mul, i32* %arrayidx15, align 4, !dbg !35, !tbaa !17
  %inc17 = add nuw nsw i32 %i5.033, 1, !dbg !36
  %cmp7 = icmp slt i32 %inc17, %len, !dbg !14
  br i1 %cmp7, label %for.body9, label %for.cond6.for.cond.cleanup8_crit_edge, !dbg !15, !llvm.loop !37
}

attributes #0 = { nofree norecurse nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6, !7, !8}
!llvm.ident = !{!9}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "Huawei Bisheng Compiler clang version 12.0.0 (0261bbf0b2fd)", isOptimized: true, runtimeVersion: 0, emissionKind: LineTablesOnly, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "loop-unroll.c", directory: "/home/AutoTuner/")
!2 = !{}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 1, !"wchar_size", i32 4}
!5 = !{i32 1, !"branch-target-enforcement", i32 0}
!6 = !{i32 1, !"sign-return-address", i32 0}
!7 = !{i32 1, !"sign-return-address-all", i32 0}
!8 = !{i32 1, !"sign-return-address-with-bkey", i32 0}
!9 = !{!"Huawei Bisheng Compiler clang version 12.0.0 (0261bbf0b2fd)"}
!10 = distinct !DISubprogram(name: "a", scope: !1, file: !1, line: 1, type: !11, scopeLine: 1, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!11 = !DISubroutineType(types: !2)
!12 = !DILocation(line: 3, column: 20, scope: !10)
!13 = !DILocation(line: 3, column: 5, scope: !10)
!14 = !DILocation(line: 7, column: 20, scope: !10)
!15 = !DILocation(line: 7, column: 5, scope: !10)
!16 = !DILocation(line: 4, column: 16, scope: !10)
!17 = !{!18, !18, i64 0}
!18 = !{!"int", !19, i64 0}
!19 = !{!"omnipotent char", !20, i64 0}
!20 = !{!"Simple C/C++ TBAA"}
!21 = !DILocation(line: 4, column: 23, scope: !10)
!22 = !DILocation(line: 4, column: 21, scope: !10)
!23 = !DILocation(line: 4, column: 9, scope: !10)
!24 = !DILocation(line: 4, column: 14, scope: !10)
!25 = !DILocation(line: 3, column: 28, scope: !10)
!26 = distinct !{!26, !13, !27, !28, !29}
!27 = !DILocation(line: 5, column: 5, scope: !10)
!28 = !{!"llvm.loop.mustprogress"}
!29 = !{!"llvm.loop.unroll.count", i32 4}
!30 = !DILocation(line: 10, column: 1, scope: !10)
!31 = !DILocation(line: 8, column: 16, scope: !10)
!32 = !DILocation(line: 8, column: 23, scope: !10)
!33 = !DILocation(line: 8, column: 21, scope: !10)
!34 = !DILocation(line: 8, column: 9, scope: !10)
!35 = !DILocation(line: 8, column: 14, scope: !10)
!36 = !DILocation(line: 7, column: 28, scope: !10)
!37 = distinct !{!37, !15, !38, !28}
!38 = !DILocation(line: 9, column: 5, scope: !10)


; TEST-1: Pass:            loop-unroll
; TEST-1-NOT: Pass:            loop-unroll

; TEST-2: Name:            for.body9
; TEST-2-NEXT: DebugLoc:        { File: loop-unroll.c, Line: 7, Column: 5 }
; TEST-2-NEXT: Function:        loop
; TEST-2-NEXT: CodeRegionType:  loop
