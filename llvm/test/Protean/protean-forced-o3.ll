; A module outside the forced selection must receive the same full pipeline as
; ordinary -O3, even when -protean is enabled.
; RUN: protean -O3 -disable-output -print-pipeline-passes -disable-pipeline-verification %s > %t.o3
; RUN: protean -protean -protean-force-recipe=C -protean-force-module=other.c \
; RUN:   -disable-output -print-pipeline-passes -disable-pipeline-verification %s > %t.forced
; RUN: diff %t.o3 %t.forced
; RUN: FileCheck %s --check-prefix=PIPELINE --implicit-check-not=protean-collect-features < %t.forced
;
; Forced runs must skip feature collection entirely. The fallback must still
; produce the ordinary -O3 IR.
; RUN: protean -O3 -o %t.o3.bc %s
; RUN: protean -protean -protean-force-recipe=C -protean-force-module=other.c \
; RUN:   -o %t.forced.bc %s 2> %t.fallback.err
; RUN: count 0 < %t.fallback.err
; RUN: llvm-diff %t.o3.bc %t.forced.bc
; RUN: protean -protean -protean-force-recipe=C -protean-force-module=selected.c \
; RUN:   -debug-pass-manager -S -o %t.recipe.ll %s 2> %t.recipe.err
; RUN: FileCheck %s --check-prefix=RECIPE --implicit-check-not=shm_open \
; RUN:   --implicit-check-not=CollectFeaturesPass < %t.recipe.err
; RUN: FileCheck %s --check-prefix=IR < %t.recipe.ll

; PIPELINE: annotation2metadata

; RECIPE: Protean: forcing recipe C for module
; RECIPE-NEXT: Protean: pipeline: annotation2metadata,forceattrs,inferattrs,coro-early,

; IR-LABEL: define i32 @f(i32 %x)
; IR-NEXT: ret i32 %x

source_filename = "selected.c"

define i32 @f(i32 %x) {
  %sum = add i32 %x, 0
  ret i32 %sum
}
