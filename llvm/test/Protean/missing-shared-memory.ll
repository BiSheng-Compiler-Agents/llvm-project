; REQUIRES: system-linux
; RUN: opt -protean -passes=protean-collect-features -disable-output %s 2>&1 | FileCheck %s

; This triggers a failed shm_open as opt has no shm from a protean parent.
; CHECK: shm_open:

define void @f() {
  ret void
}
