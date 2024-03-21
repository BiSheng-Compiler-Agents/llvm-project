; Check if error messages are shown properly for malformed YAML files.

; Missing Pass Field
; RUN: rm %t.missing-pass.yaml -rf
; RUN: sed 's#Pass:            pass##g' %S/Inputs/template.yaml > %t.missing-pass.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-pass.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-FIELD

; Missing Pass Value
; RUN: rm %t.missing-value-pass.yaml -rf
; RUN: sed 's#pass##g' %S/Inputs/template.yaml > %t.missing-value-pass.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-value-pass.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-PASS-VALUE

; Missing Name Field
; RUN: rm %t.missing-name.yaml -rf
; RUN: sed 's#Name:            for.body##g' %S/Inputs/template.yaml > %t.missing-name.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-name.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-NAME-FIELD

; Missing Name Value
; RUN: rm %t.missing-value-name.yaml -rf
; RUN: sed 's#for.body##g' %S/Inputs/template.yaml > %t.missing-value-name.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-value-name.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-NAME-VALUE

; Missing Function Field
; RUN: rm %t.missing-function.yaml -rf
; RUN: sed 's#Function:        foo##g' %S/Inputs/template.yaml > %t.missing-function.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' -auto-tuning-input=%t.missing-function.yaml 2>&1 | FileCheck %s -check-prefix=ERROR-FUNCTION-FIELD

; Missing Function Value
; RUN: rm %t.missing-value-func.yaml -rf
; RUN: sed 's#foo##g' %S/Inputs/template.yaml > %t.missing-value-func.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-value-func.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-FUNC-VALUE

; Missing CodeRegionType Field
; RUN: rm %t.missing-type.yaml -rf
; RUN: sed 's#CodeRegionType:  loop##g' %S/Inputs/template.yaml > %t.missing-type.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-type.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-CODE-REGION-TYPE-FIELD

; Missing CodeRegionType Value
; RUN: rm %t.missing-value-type.yaml -rf
; RUN: sed 's#loop##g' %S/Inputs/template.yaml > %t.missing-value-type.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-value-type.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-CODE-REGION-TYPE-VALUE

; Invalid CodeRegionType Value
; RUN: rm %t.invalid-value-type.yaml -rf
; RUN: sed 's#loop#error-type#g' %S/Inputs/template.yaml > %t.invalid-value-type.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.invalid-value-type.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-CODE-REGION-TYPE-INVALID

; Missing Param Name
; RUN: rm %t.missing-param-name.yaml -rf
; RUN: sed 's#UnrollCount##g' %S/Inputs/template.yaml > %t.missing-param-name.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-param-name.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-PARAM-NAME

; Missing Param Value
; RUN: rm %t.missing-value-param.yaml -rf
; RUN: sed 's#2##g' %S/Inputs/template.yaml > %t.missing-value-param.yaml
; RUN: not opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.missing-value-param.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=ERROR-PARAM-VALUE

; Empty Param List
; RUN: rm %t.empty-value-param-list.yaml -rf
; RUN: sed 's#\[test, test2\]#\[\]#g' %S/Inputs/template.yaml > %t.empty-value-param-list.yaml
; RUN: opt %s -S -passes='require<opt-remark-emit>,loop(loop-unroll-full)' \
; RUN:     -auto-tuning-input=%t.empty-value-param-list.yaml 2>&1 | \
; RUN:     FileCheck %s -check-prefix=VALID

; UNSUPPORTED: windows

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

; check if error massage is shown properly for malformed YAML input files.
;

; ERROR-FIELD: error: CodeRegionHash, CodeRegionType, or Pass missing.

; ERROR-NAME-FIELD: error: Remark Name expected; enable -autotuning-omit-metadata.

; ERROR-FUNCTION-FIELD: error: Remark Function Name expected; enable -autotuning-omit-metadata.

; ERROR-PASS-VALUE: error: YAML:2:1: error: expected a value of scalar type.
; ERROR-PASS-VALUE: Pass:

; ERROR-NAME-VALUE: error: YAML:3:1: error: expected a value of scalar type.
; ERROR-NAME-VALUE: Name:

; ERROR-FUNC-VALUE: error: YAML:4:1: error: expected a value of scalar type.
; ERROR-FUNC-VALUE: Function:

; ERROR-CODE-REGION-TYPE-FIELD: CodeRegionHash, CodeRegionType, or Pass missing.

; ERROR-CODE-REGION-TYPE-VALUE: error: YAML:5:1: error: expected a value of scalar type.
; ERROR-CODE-REGION-TYPE-VALUE: CodeRegionType:

; ERROR-CODE-REGION-TYPE-INVALID: Unsupported CodeRegionType:error-type

; ERROR-PARAM-NAME: error: YAML:8:5: error: argument key is missing.
; ERROR-PARAM-NAME: - : 2

; ERROR-PARAM-VALUE: error: YAML:8:5: error: expected a value of scalar type.
; ERROR-PARAM-VALUE: - UnrollCount:

; VALID-NOT: -auto-tuning-input=(input file) option failed.
