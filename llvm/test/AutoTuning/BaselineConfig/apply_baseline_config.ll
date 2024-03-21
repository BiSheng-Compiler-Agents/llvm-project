; The purpose is to test the baseline IR is the same as the 1st iteration of
; autotuning process with --use-baseline-config enabled.
; RUN: rm %t.baseline %t.firstIt -f
; RUN: opt -O3 %S/Inputs/test.ll -o %t.baseline
; RUN: opt -O3 %S/Inputs/test.ll -o %t.firstIt_baseline \
; RUN:     -auto-tuning-input=%S/Inputs/autotune_datadir/baseline_config.yaml
; RUN: cmp %t.firstIt_baseline %t.baseline

; RUN: opt -O3 %S/Inputs/test.ll -o %t.firstIt_random \
; RUN:     -auto-tuning-input=%S/Inputs/autotune_datadir/random_config.yaml
; RUN: not cmp %t.firstIt_random %t.baseline
