; REQUIRES: ACPO_AOT
; RUN: rm -f -- %t1 && opt -passes='default<O3>' --passes='protean-collect-features' --enable-protean-feature-dump --protean-dump-file=%t1 -disable-output < %s
; RUN: sed -n '1p' %t1 > %t2
; Use diff as a failing FileCheck on these long lines is very slow.
; RUN: diff %S/Inputs/header-order.txt %t2

define i32 @main() {
    ret i32 0
}
