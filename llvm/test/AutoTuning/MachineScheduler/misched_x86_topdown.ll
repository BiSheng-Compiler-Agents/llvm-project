; RUN: rm %t.topdown_result %t.misched_x86_topdown.yaml -rf
; RUN: sed  's#\[bool1\]#false#g;  s#\[bool2\]#true#g' %S/Inputs/misched_x86_template.yaml > %t.misched_x86_topdown.yaml
; RUN: llc -o - %s -march=x86-64 -mcpu=core2 -x86-early-ifcvt -enable-misched \
; RUN:           -auto-tuning-input=%t.misched_x86_topdown.yaml\
; RUN:           -verify-machineinstrs -debug-only=machine-scheduler  2>&1\
; RUN:     | FileCheck %s

; RUN: llc -o - %s -march=x86-64 -mcpu=core2 -x86-early-ifcvt -enable-misched \
; RUN:           -auto-tuning-input=%t.misched_x86_topdown.yaml\
; RUN:           -verify-machineinstrs -misched-topdown -debug-only=machine-scheduler  2>&1 \
; RUN:     | FileCheck %s -check-prefix=MIX-WITH-FLAG-TOPDOWN

; RUN: llc -o - %s -march=x86-64 -mcpu=core2 -x86-early-ifcvt -enable-misched \
; RUN:           -auto-tuning-input=%t.misched_x86_topdown.yaml\
; RUN:           -verify-machineinstrs -misched-bottomup -debug-only=machine-scheduler  2>&1 \
; RUN:     | FileCheck %s -check-prefix=MIX-WITH-FLAG-BOTTOMUP

; RUN: llc -o - %s -march=x86-64 -mcpu=core2 -x86-early-ifcvt -enable-misched  \
; RUN:           -auto-tuning-input=%t.misched_x86_topdown.yaml\
; RUN:           -verify-machineinstrs -misched-bottomup=false -misched-topdown=false -debug-only=machine-scheduler  2>&1 \
; RUN:     | FileCheck %s -check-prefix=MIX-WITH-FLAG-BIDIRECTIONAL

; REQUIRES: asserts
; UNSUPPORTED: windows
;
; Interesting MachineScheduler cases.

declare void @llvm.memcpy.p0i8.p0i8.i64(i8* nocapture, i8* nocapture, i64, i32, i1) nounwind

define fastcc void @_preextrapolate_helper() nounwind uwtable ssp {
entry:
  br i1 undef, label %for.cond.preheader, label %if.end

for.cond.preheader:                               ; preds = %entry
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* undef, i8* null, i64 128, i32 4, i1 false) nounwind
  unreachable

if.end:                                           ; preds = %entry
  ret void
}

; check if the scheduling policy defined with xml is applied
;
; CHECK: _preextrapolate_helper:%bb.1 for.cond.preheader
; CHECK: ScheduleDAGMILive::schedule starting
; CHECK-NEXT: OnlyTopDown=1 OnlyBottomUp=0


; check if the scheduling policies defined with xml and '-misched-topdown' are applied
; MIX-WITH-FLAG-TOPDOWN: _preextrapolate_helper:%bb.0 entry
; MIX-WITH-FLAG-TOPDOWN: ScheduleDAGMILive::schedule starting
; MIX-WITH-FLAG-TOPDOWN-NEXT: OnlyTopDown=1 OnlyBottomUp=0
; MIX-WITH-FLAG-TOPDOWN: _preextrapolate_helper:%bb.1 for.cond.preheader
; MIX-WITH-FLAG-TOPDOWN: ScheduleDAGMILive::schedule starting
; MIX-WITH-FLAG-TOPDOWN-NEXT: OnlyTopDown=1 OnlyBottomUp=0

; check if the scheduling policies defined with xml and '-misched-bottomup' are applied
; MIX-WITH-FLAG-BOTTOMUP: _preextrapolate_helper:%bb.0 entry
; MIX-WITH-FLAG-BOTTOMUP: ScheduleDAGMILive::schedule starting
; MIX-WITH-FLAG-BOTTOMUP-NEXT: OnlyTopDown=0 OnlyBottomUp=1
; MIX-WITH-FLAG-BOTTOMUP: _preextrapolate_helper:%bb.1 for.cond.preheader
; MIX-WITH-FLAG-BOTTOMUP: ScheduleDAGMILive::schedule starting
; MIX-WITH-FLAG-BOTTOMUP-NEXT: OnlyTopDown=1 OnlyBottomUp=0

; check if the scheduling policies defined with xml and '-misched-topdown=false' and '-misched-bottomup=false'
; are applied
; MIX-WITH-FLAG-BIDIRECTIONAL: _preextrapolate_helper:%bb.0 entry
; MIX-WITH-FLAG-BIDIRECTIONAL: ScheduleDAGMILive::schedule starting
; MIX-WITH-FLAG-BIDIRECTIONAL-NEXT: OnlyTopDown=0 OnlyBottomUp=0
; MIX-WITH-FLAG-BIDIRECTIONAL: _preextrapolate_helper:%bb.1 for.cond.preheader
; MIX-WITH-FLAG-BIDIRECTIONAL: ScheduleDAGMILive::schedule starting
; MIX-WITH-FLAG-BIDIRECTIONAL-NEXT: OnlyTopDown=1 OnlyBottomUp=0
