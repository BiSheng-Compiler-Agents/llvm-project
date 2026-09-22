; REQUIRES: system-linux
; RUN: %python %S/Inputs/check-feature-buffer.py opt %s | FileCheck %s

; Publish the feature count only when every entry fits, including when the
; buffer can hold all but the last byte of the complete feature set.
; CHECK: Complete feature set written
; CHECK-NEXT: Exact-fit feature set written
; CHECK-NEXT: Truncated feature set rejected

define void @f() {
  ret void
}
