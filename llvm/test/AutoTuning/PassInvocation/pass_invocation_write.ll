; REQUIRES: aarch64-registered-target
; RUN: rm %t.pass_invocation -rf
; RUN: opt %s -S -mtriple=aarch64-- -mcpu=tsv110 -auto-tuning-type-filter=Loop \
; RUN:     -O3 -auto-tuning-opp=%t.pass_invocation --disable-output
; RUN: FileCheck  %s  --input-file %t.pass_invocation/pass_invocation_write.ll.yaml

; Function Attrs: nounwind uwtable
define dso_local void @sum(i32* noalias %a, i32* noalias %b, i32* noalias %c, i32 %n) {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.body, %entry
  %sum.0 = phi float [ 0.000000e+00, %entry ], [ %add, %for.body ]
  %i.0 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %cmp = icmp slt i32 %i.0, %n
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %idxprom = sext i32 %i.0 to i64
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %idxprom
  %0 = load i32, i32* %arrayidx, align 4
  %idxprom1 = sext i32 %i.0 to i64
  %arrayidx2 = getelementptr inbounds i32, i32* %b, i64 %idxprom1
  %1 = load i32, i32* %arrayidx2, align 4
  %mul = mul nsw i32 %0, %1
  %conv = sitofp i32 %mul to float
  %add = fadd contract float %sum.0, %conv
  %inc = add nsw i32 %i.0, 1
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %conv3 = fptosi float %sum.0 to i32
  %arrayidx4 = getelementptr inbounds i32, i32* %c, i64 0
  store i32 %conv3, i32* %arrayidx4, align 4
  ret void
}

; CHECK: --- !AutoTuning
; CHECK-NEXT: Pass:            loop-unroll
; CHECK-NEXT: Name:            for.body
; CHECK-NEXT: Function:        sum
; CHECK-NEXT: CodeRegionType:  loop
; CHECK-NEXT: CodeRegionHash:  {{[0-9]+}}
; CHECK-NEXT: DynamicConfigs:  { UnrollCount: [ 0, 1, 8, 4, 2 ] }
; CHECK-NEXT: BaselineConfig:  { UnrollCount: '0' }
; CHECK-NEXT: Invocation:      0
; CHECK-NEXT: ...
; CHECK-NEXT: --- !AutoTuning
; CHECK-NEXT: Pass:            loop-vectorize
; CHECK-NEXT: Name:            for.body
; CHECK-NEXT: Function:        sum
; CHECK-NEXT: CodeRegionType:  loop
; CHECK-NEXT: CodeRegionHash:  {{[0-9]+}}
; CHECK-NEXT: DynamicConfigs:  { VectorizationInterleave: [ 1, 2, 4 ] }
; CHECK-NEXT: BaselineConfig:  { VectorizationInterleave: '2' }
; CHECK-NEXT: Invocation:      0
; CHECK-NEXT: ...
; CHECK-NEXT: --- !AutoTuning
; CHECK-NEXT: Pass:            loop-unroll
; CHECK-NEXT: Name:            vector.body
; CHECK-NEXT: Function:        sum
; CHECK-NEXT: CodeRegionType:  loop
; CHECK-NEXT: CodeRegionHash:  {{[0-9]+}}
; CHECK-NEXT: DynamicConfigs:  { UnrollCount: [ 0, 1, 8, 4, 2 ] }
; CHECK-NEXT: BaselineConfig:  { UnrollCount: '0' }
; CHECK-NEXT: Invocation:      1
; CHECK-NEXT: ...
