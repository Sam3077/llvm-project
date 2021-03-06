; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=thumbv8.1m.main-none-none-eabi -verify-machineinstrs -mattr=+mve %s -o - | FileCheck %s

define arm_aapcs_vfpcc <4 x i32> @bswap_4i32_t(<4 x i32> %src){
; CHECK-LABEL: bswap_4i32_t:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vrev32.8 q0, q0
; CHECK-NEXT:    bx lr
entry:
    %0 = call <4 x i32> @llvm.bswap.v4i32(<4 x i32> %src)
    ret <4 x i32> %0
}

define arm_aapcs_vfpcc <8 x i16> @bswap_8i16_t(<8 x i16> %src){
; CHECK-LABEL: bswap_8i16_t:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vrev16.8 q0, q0
; CHECK-NEXT:    bx lr
entry:
    %0 = call <8 x i16> @llvm.bswap.v8i16(<8 x i16> %src)
    ret <8 x i16> %0
}

define arm_aapcs_vfpcc <2 x i64> @bswap_2i64_t(<2 x i64> %src){
; CHECK-LABEL: bswap_2i64_t:
; CHECK:       @ %bb.0: @ %entry
; CHECK-NEXT:    vrev64.8 q1, q0
; CHECK-NEXT:    vmov q0, q1
; CHECK-NEXT:    bx lr
entry:
    %0 = call <2 x i64> @llvm.bswap.v2i64(<2 x i64> %src)
    ret <2 x i64> %0
}

declare <2 x i64> @llvm.bswap.v2i64(<2 x i64>)
declare <4 x i32> @llvm.bswap.v4i32(<4 x i32>)
declare <8 x i16> @llvm.bswap.v8i16(<8 x i16>)
