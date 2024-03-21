; REQUIRES: asserts
; RUN: rm %t.output -rf
; RUN: rm %t.inc_compile.yaml -rf
; RUN: sed 's#\[dummy-pass\]#inline#g' %S/Inputs/template.yaml > %t.temp.yaml
; RUN: sed 's#\[dummy-type\]#callsite#g' %t.temp.yaml > %t.temp2.yaml
; RUN: sed 's#\[dummy-file\]#%s#g' %t.temp2.yaml > %t.inc_compile.yaml
; RUN: opt -O3 %s -auto-tuning-input=%t.inc_compile.yaml \
; RUN:     -auto-tuning-compile-mode=CoarseGrain -print-after-all \
; RUN:     -debug-only=autotuning-compile \
; RUN:     -o %t.output 2>&1 | \
; RUN:     FileCheck %s -check-prefix=COARSEGRAIN

; RUN: rm %t.output -rf
; RUN: rm %t.inc_compile.yaml -rf
; RUN: sed 's#\[dummy-pass\]#inline#g' %S/Inputs/template.yaml > %t.temp.yaml
; RUN: sed 's#\[dummy-type\]#callsite#g' %t.temp.yaml > %t.temp2.yaml
; RUN: sed 's#\[dummy-file\]#%s#g' %t.temp2.yaml > %t.inc_compile.yaml
; RUN: opt -O3 %s -auto-tuning-input=%t.inc_compile.yaml \
; RUN:     -auto-tuning-compile-mode=FineGrain -print-after-all \
; RUN:     -debug-only=autotuning-compile \
; RUN:     -o %t.output 2>&1 | \
; RUN:     FileCheck %s -check-prefixes=FINEGRAIN-1,FINEGRAIN-INLINE

; RUN: rm %t.output -rf
; RUN: rm %t.inc_compile.yaml -rf
; RUN: sed 's#\[dummy-pass\]#loop-unroll#g' %S/Inputs/template.yaml > %t.temp.yaml
; RUN: sed 's#\[dummy-type\]#loop#g' %t.temp.yaml > %t.temp2.yaml
; RUN: sed 's#\[dummy-file\]#%s#g' %t.temp2.yaml > %t.inc_compile.yaml
; RUN: opt -O3 %s -auto-tuning-input=%t.inc_compile.yaml \
; RUN:     -auto-tuning-compile-mode=FineGrain -print-after-all \
; RUN:     -debug-only=autotuning-compile \
; RUN:     -o %t.output 2>&1 | \
; RUN:     FileCheck %s -check-prefixes=FINEGRAIN-1,FINEGRAIN-2,FINEGRAIN-UNROLL

; ModuleID = 'test.c'
source_filename = "test.c"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-linux-gnu"

; Function Attrs: argmemonly nofree norecurse nosync nounwind uwtable
define dso_local i32 @test(i32* nocapture noundef %a, i32* nocapture noundef readonly %b, i32 noundef %size) local_unnamed_addr #0 {
entry:
  %cmp11 = icmp sgt i32 %size, 0
  br i1 %cmp11, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:                               ; preds = %entry
  %wide.trip.count = zext i32 %size to i64
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body, %entry
  ret i32 undef

for.body:                                         ; preds = %for.body.preheader, %for.body
  %indvars.iv = phi i64 [ 0, %for.body.preheader ], [ %indvars.iv.next, %for.body ]
  %arrayidx = getelementptr inbounds i32, i32* %b, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4
  %arrayidx2 = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %1 = load i32, i32* %arrayidx2, align 4
  %add = add nsw i32 %1, %0
  store i32 %add, i32* %arrayidx2, align 4
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body
}

attributes #0 = { argmemonly nofree norecurse nosync nounwind uwtable "frame-pointer"="non-leaf" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+neon,+v8a" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6, !7, !8}
!llvm.ident = !{!9}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "Huawei BiSheng Compiler clang version 12.0.0 (1c7b819ced36)", isOptimized: true, runtimeVersion: 0, emissionKind: LineTablesOnly, enums: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "test.c", directory: "/home/m00629332/code/autoTuner")
!2 = !{}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 1, !"wchar_size", i32 4}
!5 = !{i32 1, !"branch-target-enforcement", i32 0}
!6 = !{i32 1, !"sign-return-address", i32 0}
!7 = !{i32 1, !"sign-return-address-all", i32 0}
!8 = !{i32 1, !"sign-return-address-with-bkey", i32 0}
!9 = !{!"Huawei BiSheng Compiler clang version 12.0.0 (1c7b819ced36)"}
!10 = distinct !DISubprogram(name: "dummy", scope: !1, file: !1, line: 1, type: !11, scopeLine: 1, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!11 = !DISubroutineType(types: !2)
!12 = !DILocation(line: 2, column: 5, scope: !10)

; COARSEGRAIN: AutoTuningCompile: Deciding to enable/disable optimization of module/functions. Pass: start
; COARSEGRAIN-NEXT: AutoTuningCompile: No change in opt pipeline for Basic/CoarseGrain incremental compilation mode.
; COARSEGRAIN-NOT: Skip pass {{.*}}: True

; FINEGRAIN-1: AutoTuningCompile: Deciding to enable/disable optimization of module/functions. Pass: start
; FINEGRAIN-1-NEXT: AutoTuningCompile: SkipPasses enabled.
; FINEGRAIN-1-NOT: Skip pass {{.*}}: False
; FINEGRAIN-1: AutoTuningCompile: Deciding to enable/disable optimization of module/functions. Pass: inline
; FINEGRAIN-INLINE: AutoTuningCompile: SkipPasses disabled.
; FINEGRAIN-INLINE: Skip pass 'InlinerPass': False
; FINEGRAIN-INLINE-NEXT: *** IR Dump After InlinerPass
; FINEGRAIN-INLINE-NOT: Skip pass {{.*}}: True

; FINEGRAIN-2: AutoTuningCompile: Old decision (SkipPasses = True ) continued.
; FINEGRAIN-2-NOT: Skip pass {{.*}}: False
; FINEGRAIN-2: AutoTuningCompile: Deciding to enable/disable optimization of module/functions. Pass: loop-unroll
; FINEGRAIN-UNROLL: AutoTuningCompile: SkipPasses disabled.
; FINEGRAIN-UNROLL-NOT: Skip pass {{.*}}: True
