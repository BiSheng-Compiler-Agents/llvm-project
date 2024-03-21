; UNSUPPORTED: windows
; RUN: sed 's#\[number\]#0#g; s#\[name\]#for.body#g' \
; RUN:     %S/Inputs/unroll_template.yaml > %t.DEFAULT.yaml
; RUN: opt --disable-output %s -S -passes='require<autotuning-dump>' \
; RUN:     -auto-tuning-input=%t.DEFAULT.yaml -auto-tuning-config-id=1
; RUN: cat %T/../autotune_datadir/create-data-dir.ll/1.ll | FileCheck %s
; RUN: rm -rf %T/../autotune_datadir/*

; RUN: cp %t.DEFAULT.yaml %T/../autotune_datadir/config.yaml
; RUN: opt %s -S -passes='require<autotuning-dump>' -auto-tuning-config-id=1
; RUN: cat %T/../autotune_datadir/create-data-dir.ll/1.ll | FileCheck %s
; RUN: rm -rf %T/../autotune_datadir/*

; RUN: cp %t.DEFAULT.yaml %T/../autotune_datadir/config.yaml
; RUN: opt %s -S -passes='require<autotuning-dump>' -enable-autotuning-dump
; RUN: echo -n %T/../autotune_datadir/IR_files/ > %t.filename
; RUN: echo -n "create-data-dir.ll/" >> %t.filename
; RUN: echo -n %s | sed 's#/#_#g' >> %t.filename
; RUN: echo -n ".ll" >> %t.filename
; RUN: cat %t.filename | xargs cat | FileCheck %s
; RUN: rm -rf %T/../autotune_datadir

; ModuleID = 'search.c'
source_filename = "search.c"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-linux-gnu"

; Function Attrs: argmemonly nofree norecurse nosync nounwind readonly uwtable
define dso_local i32 @search(ptr nocapture noundef readonly %Arr, i32 noundef %Value, i32 noundef %Size) {
entry:
  %cmp5 = icmp sgt i32 %Size, 0
  br i1 %cmp5, label %for.body.preheader, label %for.end

for.body.preheader:                               ; preds = %entry
  %wide.trip.count = zext i32 %Size to i64
  br label %for.body

for.body:                                         ; preds = %for.body.preheader, %for.inc
  %indvars.iv = phi i64 [ 0, %for.body.preheader ], [ %indvars.iv.next, %for.inc ]
  %arrayidx = getelementptr inbounds i32, ptr %Arr, i64 %indvars.iv
  %0 = load i32, ptr %arrayidx, align 4
  %cmp1 = icmp eq i32 %0, %Value
  br i1 %cmp1, label %for.end.loopexit.split.loop.exit, label %for.inc

for.inc:                                          ; preds = %for.body
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count
  br i1 %exitcond.not, label %for.end, label %for.body

for.end.loopexit.split.loop.exit:                 ; preds = %for.body
  %1 = trunc i64 %indvars.iv to i32
  br label %for.end

for.end:                                          ; preds = %for.inc, %for.end.loopexit.split.loop.exit, %entry
  %Idx.0.lcssa = phi i32 [ 0, %entry ], [ %1, %for.end.loopexit.split.loop.exit ], [ %Size, %for.inc ]
  ret i32 %Idx.0.lcssa
}

; Check that only loop body is inside the IR File.
; CHECK-LABEL: for.body:                                         ; preds =
; CHECK-NEXT: %indvars.iv = phi i64 [ 0, %for.body.preheader ], [ %indvars.iv.next, %for.inc ]
; CHECK-NEXT: %arrayidx = getelementptr inbounds i32, ptr %Arr, i64 %indvars.iv
; CHECK-NEXT: %0 = load i32, ptr %arrayidx, align 4
; CHECK-NEXT: %cmp1 = icmp eq i32 %0, %Value
; CHECK-NEXT: br i1 %cmp1, label %for.end.loopexit.split.loop.exit, label %for.inc
