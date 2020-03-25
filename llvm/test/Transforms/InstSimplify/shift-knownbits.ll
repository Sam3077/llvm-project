; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt < %s -instsimplify -S | FileCheck %s

; If any bits of the shift amount are known to make it exceed or equal
; the number of bits in the type, the shift causes undefined behavior.

define i32 @shl_amount_is_known_bogus(i32 %a, i32 %b) {
; CHECK-LABEL: @shl_amount_is_known_bogus(
; CHECK-NEXT:    ret i32 undef
;
  %or = or i32 %b, 32
  %shl = shl i32 %a, %or
  ret i32 %shl
}

; Check some weird types and the other shift ops.

define i31 @lshr_amount_is_known_bogus(i31 %a, i31 %b) {
; CHECK-LABEL: @lshr_amount_is_known_bogus(
; CHECK-NEXT:    ret i31 undef
;
  %or = or i31 %b, 31
  %shr = lshr i31 %a, %or
  ret i31 %shr
}

define i33 @ashr_amount_is_known_bogus(i33 %a, i33 %b) {
; CHECK-LABEL: @ashr_amount_is_known_bogus(
; CHECK-NEXT:    ret i33 undef
;
  %or = or i33 %b, 33
  %shr = ashr i33 %a, %or
  ret i33 %shr
}


; If all valid bits of the shift amount are known 0, there's no shift.
; It doesn't matter if high bits are set because that would be undefined.
; Therefore, the only possible valid result of these shifts is %a.

define i16 @ashr_amount_is_zero(i16 %a, i16 %b) {
; CHECK-LABEL: @ashr_amount_is_zero(
; CHECK-NEXT:    ret i16 [[A:%.*]]
;
  %and = and i16 %b, 65520 ; 0xfff0
  %shr = ashr i16 %a, %and
  ret i16 %shr
}

define i300 @lshr_amount_is_zero(i300 %a, i300 %b) {
; CHECK-LABEL: @lshr_amount_is_zero(
; CHECK-NEXT:    ret i300 [[A:%.*]]
;
  %and = and i300 %b, 2048
  %shr = lshr i300 %a, %and
  ret i300 %shr
}

define i9 @shl_amount_is_zero(i9 %a, i9 %b) {
; CHECK-LABEL: @shl_amount_is_zero(
; CHECK-NEXT:    ret i9 [[A:%.*]]
;
  %and = and i9 %b, 496 ; 0x1f0
  %shl = shl i9 %a, %and
  ret i9 %shl
}


; Verify that we've calculated the log2 boundary of valid bits correctly for a weird type.

define i9 @shl_amount_is_not_known_zero(i9 %a, i9 %b) {
; CHECK-LABEL: @shl_amount_is_not_known_zero(
; CHECK-NEXT:    [[AND:%.*]] = and i9 [[B:%.*]], -8
; CHECK-NEXT:    [[SHL:%.*]] = shl i9 [[A:%.*]], [[AND]]
; CHECK-NEXT:    ret i9 [[SHL]]
;
  %and = and i9 %b, 504 ; 0x1f8
  %shl = shl i9 %a, %and
  ret i9 %shl
}


; For vectors, we need all scalar elements to meet the requirements to optimize.

define <2 x i32> @ashr_vector_bogus(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: @ashr_vector_bogus(
; CHECK-NEXT:    ret <2 x i32> undef
;
  %or = or <2 x i32> %b, <i32 32, i32 32>
  %shr = ashr <2 x i32> %a, %or
  ret <2 x i32> %shr
}

; FIXME: This is undef, but computeKnownBits doesn't handle the union.
define <2 x i32> @shl_vector_bogus(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: @shl_vector_bogus(
; CHECK-NEXT:    [[OR:%.*]] = or <2 x i32> [[B:%.*]], <i32 32, i32 64>
; CHECK-NEXT:    [[SHL:%.*]] = shl <2 x i32> [[A:%.*]], [[OR]]
; CHECK-NEXT:    ret <2 x i32> [[SHL]]
;
  %or = or <2 x i32> %b, <i32 32, i32 64>
  %shl = shl <2 x i32> %a, %or
  ret <2 x i32> %shl
}

define <2 x i32> @lshr_vector_zero(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: @lshr_vector_zero(
; CHECK-NEXT:    ret <2 x i32> [[A:%.*]]
;
  %and = and <2 x i32> %b, <i32 64, i32 256>
  %shr = lshr <2 x i32> %a, %and
  ret <2 x i32> %shr
}

; Make sure that weird vector types work too.
define <2 x i15> @shl_vector_zero(<2 x i15> %a, <2 x i15> %b) {
; CHECK-LABEL: @shl_vector_zero(
; CHECK-NEXT:    ret <2 x i15> [[A:%.*]]
;
  %and = and <2 x i15> %b, <i15 1024, i15 1024>
  %shl = shl <2 x i15> %a, %and
  ret <2 x i15> %shl
}

define <2 x i32> @shl_vector_for_real(<2 x i32> %a, <2 x i32> %b) {
; CHECK-LABEL: @shl_vector_for_real(
; CHECK-NEXT:    [[AND:%.*]] = and <2 x i32> [[B:%.*]], <i32 3, i32 3>
; CHECK-NEXT:    [[SHL:%.*]] = shl <2 x i32> [[A:%.*]], [[AND]]
; CHECK-NEXT:    ret <2 x i32> [[SHL]]
;
  %and = and <2 x i32> %b, <i32 3, i32 3> ; a necessary mask op
  %shl = shl <2 x i32> %a, %and
  ret <2 x i32> %shl
}


; We calculate the valid bits of the shift using log2, and log2 of 1 (the type width) is 0.
; That should be ok. Either the shift amount is 0 or invalid (1), so we can always return %a.

define i1 @shl_i1(i1 %a, i1 %b) {
; CHECK-LABEL: @shl_i1(
; CHECK-NEXT:    ret i1 [[A:%.*]]
;
  %shl = shl i1 %a, %b
  ret i1 %shl
}

; Simplify count leading/trailing zeros to zero if all valid bits are shifted out.

declare i32 @llvm.cttz.i32(i32, i1) nounwind readnone
declare i32 @llvm.ctlz.i32(i32, i1) nounwind readnone
declare <2 x i8> @llvm.cttz.v2i8(<2 x i8>, i1) nounwind readnone
declare <2 x i8> @llvm.ctlz.v2i8(<2 x i8>, i1) nounwind readnone

define i32 @lshr_ctlz_zero_is_undef(i32 %x) {
; CHECK-LABEL: @lshr_ctlz_zero_is_undef(
; CHECK-NEXT:    ret i32 0
;
  %ct = call i32 @llvm.ctlz.i32(i32 %x, i1 true)
  %sh = lshr i32 %ct, 5
  ret i32 %sh
}

define i32 @lshr_cttz_zero_is_undef(i32 %x) {
; CHECK-LABEL: @lshr_cttz_zero_is_undef(
; CHECK-NEXT:    ret i32 0
;
  %ct = call i32 @llvm.cttz.i32(i32 %x, i1 true)
  %sh = lshr i32 %ct, 5
  ret i32 %sh
}

define <2 x i8> @lshr_ctlz_zero_is_undef_splat_vec(<2 x i8> %x) {
; CHECK-LABEL: @lshr_ctlz_zero_is_undef_splat_vec(
; CHECK-NEXT:    ret <2 x i8> zeroinitializer
;
  %ct = call <2 x i8> @llvm.ctlz.v2i8(<2 x i8> %x, i1 true)
  %sh = lshr <2 x i8> %ct, <i8 3, i8 3>
  ret <2 x i8> %sh
}

define i8 @lshr_ctlz_zero_is_undef_vec(<2 x i8> %x) {
; CHECK-LABEL: @lshr_ctlz_zero_is_undef_vec(
; CHECK-NEXT:    ret i8 0
;
  %ct = call <2 x i8> @llvm.ctlz.v2i8(<2 x i8> %x, i1 true)
  %sh = lshr <2 x i8> %ct, <i8 3, i8 0>
  %ex = extractelement <2 x i8> %sh, i32 0
  ret i8 %ex
}

define <2 x i8> @lshr_cttz_zero_is_undef_splat_vec(<2 x i8> %x) {
; CHECK-LABEL: @lshr_cttz_zero_is_undef_splat_vec(
; CHECK-NEXT:    ret <2 x i8> zeroinitializer
;
  %ct = call <2 x i8> @llvm.cttz.v2i8(<2 x i8> %x, i1 true)
  %sh = lshr <2 x i8> %ct, <i8 3, i8 3>
  ret <2 x i8> %sh
}

define i8 @lshr_cttz_zero_is_undef_vec(<2 x i8> %x) {
; CHECK-LABEL: @lshr_cttz_zero_is_undef_vec(
; CHECK-NEXT:    ret i8 0
;
  %ct = call <2 x i8> @llvm.cttz.v2i8(<2 x i8> %x, i1 true)
  %sh = lshr <2 x i8> %ct, <i8 3, i8 0>
  %ex = extractelement <2 x i8> %sh, i32 0
  ret i8 %ex
}

