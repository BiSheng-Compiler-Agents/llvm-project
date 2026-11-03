; Test -file-override and -default-opt, which pick per-module optimization
; (a fixed recipe, O3 or SA) based on the module's source filename.

; A module not named by any override gets the -default-opt=O3 pipeline, which
; must match plain -O3 and must not add the trailing feature collection.
; RUN: protean -O3 -disable-output -print-pipeline-passes \
; RUN:   -disable-pipeline-verification %s > %t.o3
; RUN: protean -protean -default-opt=O3 -file-override=other.c=C \
; RUN:   -disable-output -print-pipeline-passes \
; RUN:   -disable-pipeline-verification %s > %t.default
; RUN: diff %t.o3 %t.default
; RUN: FileCheck %s --check-prefix=O3 \
; RUN:   --implicit-check-not=protean-collect-features < %t.default

; An O3 override wins over the default SA and skips annealing entirely.
; RUN: protean -protean -file-override=selected.c=O3 -disable-output \
; RUN:   -print-pipeline-passes -disable-pipeline-verification %s > %t.override
; RUN: diff %t.o3 %t.override

; A recipe override replaces the pipeline for the matching module. Keywords and
; recipes are case-insensitive.
; RUN: protean -protean -default-opt=O3 -file-override=selected.c=c \
; RUN:   -disable-output -print-pipeline-passes \
; RUN:   -disable-pipeline-verification %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=RECIPE-C \
; RUN:     --implicit-check-not=protean-collect-features
; RUN: protean -protean -file-override=selected.c=o3 -disable-output \
; RUN:   -print-pipeline-passes -disable-pipeline-verification %s > %t.lower
; RUN: diff %t.o3 %t.lower

; Later pairs take precedence over earlier ones.
; RUN: protean -protean -file-override=selected.c=C \
; RUN:   -file-override=selected.c=O3 -disable-output -print-pipeline-passes \
; RUN:   -disable-pipeline-verification %s > %t.later-o3
; RUN: diff %t.o3 %t.later-o3
; RUN: protean -protean -file-override=selected.c=O3 \
; RUN:   -file-override=selected.c=E -disable-output -print-pipeline-passes \
; RUN:   -disable-pipeline-verification %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=RECIPE-E

; Multi-letter recipes apply each letter's passes in order. gvn-hoist only
; appears in recipe C and loop-deletion only in recipe E.
; RUN: protean -protean -file-override=selected.c=CE -disable-output \
; RUN:   -print-pipeline-passes -disable-pipeline-verification %s 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=ORDER-CE
; RUN: protean -protean -file-override=selected.c=EC -disable-output \
; RUN:   -print-pipeline-passes -disable-pipeline-verification %s 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=ORDER-EC

; The O3 fallback produces the same IR as plain -O3, without printing anything
; or running the trailing feature collection.
; RUN: protean -O3 -o %t.o3.bc %s
; RUN: protean -protean -default-opt=O3 -file-override=other.c=C \
; RUN:   -debug-pass-manager -o %t.default.bc %s 2> %t.default.err
; RUN: FileCheck %s --check-prefix=PM --implicit-check-not=CollectFeaturesPass \
; RUN:   < %t.default.err
; RUN: llvm-diff %t.o3.bc %t.default.bc

; The recipe is applied to the IR, again without feature collection.
; RUN: protean -protean -default-opt=O3 -file-override=selected.c=C \
; RUN:   -debug-pass-manager -S -o %t.recipe.ll %s 2> %t.recipe.err
; RUN: FileCheck %s --check-prefix=PM --implicit-check-not=CollectFeaturesPass \
; RUN:   < %t.recipe.err
; RUN: FileCheck %s --check-prefix=IR < %t.recipe.ll

; O3: {{^}}annotation2metadata,forceattrs,inferattrs,coro-early,function<eager-inv>(ee-instrument<>,

; RECIPE-C: Protean: forcing recipe C for module
; RECIPE-C-NEXT: {{^}}annotation2metadata,forceattrs,inferattrs,coro-early,function<eager-inv>(sroa<modify-cfg>,gvn-hoist,

; RECIPE-E: Protean: forcing recipe E for module
; RECIPE-E-NEXT: {{^}}annotation2metadata,forceattrs,inferattrs,coro-early,function<eager-inv>(loop-simplify,

; ORDER-CE: gvn-hoist
; ORDER-CE-SAME: loop-deletion

; ORDER-EC: loop-deletion
; ORDER-EC-SAME: gvn-hoist

; PM: Running pass: InstCombinePass on f

; IR-LABEL: define i32 @f(i32 %x)
; IR-NEXT: ret i32 %x

source_filename = "selected.c"

define i32 @f(i32 %x) {
  %sum = add i32 %x, 0
  ret i32 %sum
}
