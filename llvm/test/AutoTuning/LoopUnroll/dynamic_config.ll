; RUN: rm %t.default_opp -rf
; RUN: opt  %s -S -auto-tuning-opp=%t.default_opp -auto-tuning-type-filter=Loop \
; RUN:     -passes='require<opt-remark-emit>,loop(loop-unroll-full)' --disable-output
; RUN: FileCheck  %s  --input-file %t.default_opp/dynamic_config.ll.yaml

; Function Attrs: nofree norecurse nounwind uwtable
define dso_local void @transform(i64* nocapture %W) local_unnamed_addr{
entry:
  br label %for.body

for.body:                                         ; preds = %entry, %for.body
  %i.037 = phi i32 [ 16, %entry ], [ %inc, %for.body ]
  %sub = add nsw i32 %i.037, -3
  %idxprom = sext i32 %sub to i64
  %arrayidx = getelementptr inbounds i64, i64* %W, i64 %idxprom
  %0 = load i64, i64* %arrayidx, align 8
  %sub1 = add nsw i32 %i.037, -6
  %idxprom2 = sext i32 %sub1 to i64
  %arrayidx3 = getelementptr inbounds i64, i64* %W, i64 %idxprom2
  %1 = load i64, i64* %arrayidx3, align 8
  %xor = xor i64 %1, %0
  %idxprom4 = zext i32 %i.037 to i64
  %arrayidx5 = getelementptr inbounds i64, i64* %W, i64 %idxprom4
  store i64 %xor, i64* %arrayidx5, align 8
  %inc = add nuw nsw i32 %i.037, 1
  %cmp = icmp ult i32 %i.037, 79
  br i1 %cmp, label %for.body, label %for.body8.preheader

for.body8.preheader:                              ; preds = %for.body
  br label %for.body8

for.body8:                                        ; preds = %for.body8.preheader, %for.body8
  %indvars.iv = phi i64 [ 80, %for.body8.preheader ], [ %indvars.iv.next, %for.body8 ]
  %2 = add nsw i64 %indvars.iv, -4
  %arrayidx11 = getelementptr inbounds i64, i64* %W, i64 %2
  %3 = load i64, i64* %arrayidx11, align 8
  %4 = add nsw i64 %indvars.iv, -5
  %arrayidx14 = getelementptr inbounds i64, i64* %W, i64 %4
  %5 = load i64, i64* %arrayidx14, align 8
  %xor15 = xor i64 %5, %3
  %arrayidx17 = getelementptr inbounds i64, i64* %W, i64 %indvars.iv
  store i64 %xor15, i64* %arrayidx17, align 8
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp ne i64 %indvars.iv.next, 256
  br i1 %exitcond, label %for.body8, label %for.end20

for.end20:                                        ; preds = %for.body8
  ret void
}

; CHECK: --- !AutoTuning
; CHECK: DynamicConfigs:  { UnrollCount: [ 0, 1, 64, 16, 32 ]
; CHECK: ...
; CHECK-NEXT: --- !AutoTuning
; CHECK: DynamicConfigs:  { UnrollCount: [ 0, 1, 64, 16, 32 ]
; CHECK: ...
