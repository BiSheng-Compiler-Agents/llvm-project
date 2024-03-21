; REQUIRES: asserts
; CodeRegionHash matches for the first code region only. AutoTuner will find
; match for one code region when hash matching is enabled. AutoTuner will find
; match for all three code regions when hash matching is disabl3ed.
 
; RUN: rm -rf %t.loop_nest.txt
; RUN: opt %s -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -debug-only=autotuning -auto-tuning-input=%S/Inputs/loop_nest.yaml \
; RUN:     --disable-output &> %t.loop_nest.txt
; RUN: grep 'UnrollCount is set' %t.loop_nest.txt | wc -l | \
; RUN:     FileCheck %s -check-prefix=HASH_MATCHING_ENABLED

; RUN: rm -rf %t.loop_nest.txt
; RUN: opt %s -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%S/Inputs/loop_nest.yaml -debug-only=autotuning \
; RUN:     -auto-tuning-code-region-matching-hash=false --disable-output &> %t.loop_nest.txt
; RUN: grep 'UnrollCount is set' %t.loop_nest.txt | wc -l | \
; RUN:     FileCheck %s -check-prefix=HASH_MATCHING_DISABLED

; ModuleID = 'loop-nest.c'
source_filename = "loop-nest.c"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-linux-gnu"

; Function Attrs: nofree norecurse nounwind uwtable
define dso_local void @loop_nest(i32 %ni, i32 %nj, i32 %nk, i32 %alpha, i32 %beta, i32** nocapture readonly %A, i32** nocapture readonly %B, i32** nocapture readonly %C) local_unnamed_addr #0 !dbg !10 {
entry:
  %cmp41 = icmp sgt i32 %ni, 0, !dbg !12
  br i1 %cmp41, label %for.cond1.preheader.lr.ph, label %for.end23, !dbg !13

for.cond1.preheader.lr.ph:                        ; preds = %entry
  %cmp238 = icmp slt i32 %nk, 1
  %cmp536 = icmp slt i32 %nj, 1
  %wide.trip.count51 = zext i32 %ni to i64, !dbg !12
  %wide.trip.count47 = zext i32 %nk to i64
  %wide.trip.count = zext i32 %nj to i64
  %brmerge = or i1 %cmp238, %cmp536
  br label %for.cond1.preheader, !dbg !13

for.cond1.preheader:                              ; preds = %for.cond1.preheader.lr.ph, %for.inc21
  %indvars.iv49 = phi i64 [ 0, %for.cond1.preheader.lr.ph ], [ %indvars.iv.next50, %for.inc21 ]
  br i1 %brmerge, label %for.inc21, label %for.cond4.preheader.us.preheader, !dbg !14

for.cond4.preheader.us.preheader:                 ; preds = %for.cond1.preheader
  %arrayidx15 = getelementptr inbounds i32*, i32** %C, i64 %indvars.iv49
  %arrayidx = getelementptr inbounds i32*, i32** %A, i64 %indvars.iv49
  %.pre = load i32*, i32** %arrayidx, align 8, !tbaa !15
  %.pre53 = load i32*, i32** %arrayidx15, align 8, !tbaa !15
  br label %for.cond4.preheader.us, !dbg !14

for.cond4.preheader.us:                           ; preds = %for.cond4.preheader.us.preheader, %for.cond4.for.inc18_crit_edge.us
  %indvars.iv45 = phi i64 [ 0, %for.cond4.preheader.us.preheader ], [ %indvars.iv.next46, %for.cond4.for.inc18_crit_edge.us ]
  %arrayidx8.us = getelementptr inbounds i32, i32* %.pre, i64 %indvars.iv45
  %arrayidx10.us = getelementptr inbounds i32*, i32** %B, i64 %indvars.iv45
  %0 = load i32*, i32** %arrayidx10.us, align 8, !tbaa !15
  br label %for.body6.us, !dbg !19

for.body6.us:                                     ; preds = %for.cond4.preheader.us, %for.body6.us
  %indvars.iv = phi i64 [ 0, %for.cond4.preheader.us ], [ %indvars.iv.next, %for.body6.us ]
  %1 = load i32, i32* %arrayidx8.us, align 4, !dbg !20, !tbaa !21
  %mul.us = mul nsw i32 %1, %alpha, !dbg !23
  %arrayidx12.us = getelementptr inbounds i32, i32* %0, i64 %indvars.iv, !dbg !24
  %2 = load i32, i32* %arrayidx12.us, align 4, !dbg !24, !tbaa !21
  %mul13.us = mul nsw i32 %mul.us, %2, !dbg !25
  %arrayidx17.us = getelementptr inbounds i32, i32* %.pre53, i64 %indvars.iv, !dbg !26
  %3 = load i32, i32* %arrayidx17.us, align 4, !dbg !27, !tbaa !21
  %add.us = add nsw i32 %3, %mul13.us, !dbg !27
  store i32 %add.us, i32* %arrayidx17.us, align 4, !dbg !27, !tbaa !21
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1, !dbg !28
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count, !dbg !29
  br i1 %exitcond.not, label %for.cond4.for.inc18_crit_edge.us, label %for.body6.us, !dbg !19, !llvm.loop !30

for.cond4.for.inc18_crit_edge.us:                 ; preds = %for.body6.us
  %indvars.iv.next46 = add nuw nsw i64 %indvars.iv45, 1, !dbg !33
  %exitcond48.not = icmp eq i64 %indvars.iv.next46, %wide.trip.count47, !dbg !34
  br i1 %exitcond48.not, label %for.inc21, label %for.cond4.preheader.us, !dbg !14, !llvm.loop !35

for.inc21:                                        ; preds = %for.cond4.for.inc18_crit_edge.us, %for.cond1.preheader
  %indvars.iv.next50 = add nuw nsw i64 %indvars.iv49, 1, !dbg !37
  %exitcond52.not = icmp eq i64 %indvars.iv.next50, %wide.trip.count51, !dbg !12
  br i1 %exitcond52.not, label %for.end23, label %for.cond1.preheader, !dbg !13, !llvm.loop !38

for.end23:                                        ; preds = %for.inc21, %entry
  ret void, !dbg !40
}

attributes #0 = { nofree norecurse nounwind uwtable "disable-tail-calls"="false" "frame-pointer"="non-leaf" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6, !7, !8}
!llvm.ident = !{!9}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "Huawei BiSheng Compiler clang version 12.0.0 (clang-a279e099a09a flang-9a86b70390a7)", isOptimized: true, runtimeVersion: 0, emissionKind: LineTablesOnly, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "loop-nest.c", directory: "/home/m00629332/code/autoTuner")
!2 = !{}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 1, !"wchar_size", i32 4}
!5 = !{i32 1, !"branch-target-enforcement", i32 0}
!6 = !{i32 1, !"sign-return-address", i32 0}
!7 = !{i32 1, !"sign-return-address-all", i32 0}
!8 = !{i32 1, !"sign-return-address-with-bkey", i32 0}
!9 = !{!"Huawei BiSheng Compiler clang version 12.0.0 (clang-a279e099a09a flang-9a86b70390a7)"}
!10 = distinct !DISubprogram(name: "loop_nest", scope: !1, file: !1, line: 1, type: !11, scopeLine: 5, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!11 = !DISubroutineType(types: !2)
!12 = !DILocation(line: 8, column: 17, scope: !10)
!13 = !DILocation(line: 8, column: 3, scope: !10)
!14 = !DILocation(line: 9, column: 5, scope: !10)
!15 = !{!16, !16, i64 0}
!16 = !{!"any pointer", !17, i64 0}
!17 = !{!"omnipotent char", !18, i64 0}
!18 = !{!"Simple C/C++ TBAA"}
!19 = !DILocation(line: 10, column: 8, scope: !10)
!20 = !DILocation(line: 11, column: 23, scope: !10)
!21 = !{!22, !22, i64 0}
!22 = !{!"int", !17, i64 0}
!23 = !DILocation(line: 11, column: 21, scope: !10)
!24 = !DILocation(line: 11, column: 33, scope: !10)
!25 = !DILocation(line: 11, column: 31, scope: !10)
!26 = !DILocation(line: 11, column: 4, scope: !10)
!27 = !DILocation(line: 11, column: 12, scope: !10)
!28 = !DILocation(line: 10, column: 29, scope: !10)
!29 = !DILocation(line: 10, column: 22, scope: !10)
!30 = distinct !{!30, !19, !31, !32}
!31 = !DILocation(line: 11, column: 39, scope: !10)
!32 = !{!"llvm.loop.mustprogress"}
!33 = !DILocation(line: 9, column: 26, scope: !10)
!34 = !DILocation(line: 9, column: 19, scope: !10)
!35 = distinct !{!35, !14, !36, !32}
!36 = !DILocation(line: 12, column: 5, scope: !10)
!37 = !DILocation(line: 8, column: 24, scope: !10)
!38 = distinct !{!38, !13, !39, !32}
!39 = !DILocation(line: 13, column: 3, scope: !10)
!40 = !DILocation(line: 15, column: 1, scope: !10)

; HASH_MATCHING_ENABLED: 1
; HASH_MATCHING_DISABLED: 3
