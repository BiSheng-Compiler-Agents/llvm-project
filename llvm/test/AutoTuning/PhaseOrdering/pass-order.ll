; Run different orders of opt passes and verify that the order is respected
; -------------------------------------------------------------------------
; Check to see if the order is correct, trivial case (autotuning disabled)
; RUN: opt %s -debug-pass-manager -S 2>&1 | FileCheck %s -check-prefix=DISABLE

; One pass:
; RUN: rm %t.onepass_order.yaml -rf
; RUN: sed 's#\[filename\]#%s#g; s#\[pass\]#\[loop-extract\]#g' \
; RUN:    %S/Inputs/template.yaml > %t.onepass_order.yaml
; RUN: opt %s -debug-pass-manager -S -auto-tuning-input=%t.onepass_order.yaml \
; RUN:    2>&1 | FileCheck %s -check-prefix=ONEPASS

; Two passes (A->B):
; RUN: rm %t.twopass_order.yaml -rf
; RUN: sed 's#\[filename\]#%s#g; s#\[pass\]#\[loop-extract,strip\]#g' \
; RUN:    %S/Inputs/template.yaml > %t.twopass_order.yaml
; RUN: opt %s -debug-pass-manager -S -auto-tuning-input=%t.twopass_order.yaml \
; RUN:    2>&1 | FileCheck %s -check-prefix=TWOPASS_AB

; Two passes (B->A):
; RUN: rm %t.twopass_ba_order.yaml -rf
; RUN: sed 's#\[filename\]#%s#g; s#\[pass\]#\[strip, loop-extract\]#g' \
; RUN:    %S/Inputs/template.yaml > %t.twopass_ba_order.yaml
; RUN: opt %s -debug-pass-manager -S -auto-tuning-input=%t.twopass_ba_order.yaml \
; RUN:    2>&1 | FileCheck %s -check-prefix=TWOPASS_BA

; candidate IR that can change based on many optimizations
; for now just use the IR in the LoopUnroll test file
define void @foo(i32* nocapture %a) {
entry:
  br label %for.body

for.body:                                         ; preds = %for.body, %entry
  %indvars.iv = phi i64 [ 0, %entry ], [ %indvars.iv.next, %for.body ]
  %arrayidx = getelementptr inbounds i32, i32* %a, i64 %indvars.iv
  %0 = load i32, i32* %arrayidx, align 4
  %inc = add nsw i32 %0, 1
  store i32 %inc, i32* %arrayidx, align 4
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond = icmp eq i64 %indvars.iv.next, 64
  br i1 %exitcond, label %for.end, label %for.body

for.end:                                          ; preds = %for.body
  ret void
}

; DISABLE-NOT: Running pass: LoopExtractorPass on [module]
; DISABLE-NOT: Running pass: StripSymbolsPass on [module] 
; DISABLE: Running pass: VerifierPass on [module]
; DISABLE: Running pass: PrintModulePass on [module]

; ONEPASS-NOT: Running pass: StripSymbolsPass on [module]
; ONEPASS: Running pass: LoopExtractorPass on [module]
; ONEPASS: Running pass: VerifierPass on [module]
; ONEPASS: Running pass: PrintModulePass on [module]

; TWOPASS_AB: Running pass: LoopExtractorPass on [module]
; TWOPASS_AB: Running pass: StripSymbolsPass on [module]
; TWOPASS_AB: Running pass: VerifierPass on [module]
; TWOPASS_AB: Running pass: PrintModulePass on [module]

; TWOPASS_BA: Running pass: StripSymbolsPass on [module]
; TWOPASS_BA: Running pass: LoopExtractorPass on [module]
; TWOPASS_BA: Running pass: VerifierPass on [module]
; TWOPASS_BA: Running pass: PrintModulePass on [module]
