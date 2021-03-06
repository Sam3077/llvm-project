; RUN: llc -mtriple=amdgcn-amd-amdhsa -mcpu=gfx908 -verify-machineinstrs < %s | FileCheck -enable-var-scope -check-prefixes=GCN,GFX9 %s

; GCN-LABEL: {{^}}shl_base_atomicrmw_global_ptr:
; GCN: v_add_co_u32_e32 v[[EXTRA_LO:[0-9]+]], vcc, 0x80, v4
; GCN: v_addc_co_u32_e32 v[[EXTRA_HI:[0-9]+]], vcc, 0, v5, vcc
; GCN: v_lshlrev_b64 v{{\[}}[[LO:[0-9]+]]:[[HI:[0-9]+]]{{\]}}, 2, v[4:5]
; GCN: v_mov_b32_e32 [[THREE:v[0-9]+]], 3
; GCN: global_atomic_and v{{\[}}[[LO]]:[[HI]]{{\]}}, [[THREE]], off offset:512
; GCN: global_store_dwordx2 v{{\[[0-9]+:[0-9]+\]}}, v{{\[}}[[EXTRA_LO]]:[[EXTRA_HI]]{{\]}}
define void @shl_base_atomicrmw_global_ptr(i32 addrspace(1)* %out, i64 addrspace(1)* %extra.use, [512 x i32] addrspace(1)* %ptr) #0 {
  %arrayidx0 = getelementptr inbounds [512 x i32], [512 x i32] addrspace(1)* %ptr, i64 0, i64 32
  %cast = ptrtoint i32 addrspace(1)* %arrayidx0 to i64
  %shl = shl i64 %cast, 2
  %castback = inttoptr i64 %shl to i32 addrspace(1)*
  %val = atomicrmw and i32 addrspace(1)* %castback, i32 3 seq_cst
  store volatile i64 %cast, i64 addrspace(1)* %extra.use, align 4
  ret void
}

; GCN-LABEL: {{^}}shl_base_global_ptr_global_atomic_fadd:
; GCN: v_add_co_u32_e32 v[[EXTRA_LO:[0-9]+]], vcc, 0x80, v4
; GCN: v_addc_co_u32_e32 v[[EXTRA_HI:[0-9]+]], vcc, 0, v5, vcc
; GCN: v_lshlrev_b64 v{{\[}}[[LO:[0-9]+]]:[[HI:[0-9]+]]{{\]}}, 2, v[4:5]
; GCN: v_mov_b32_e32 [[K:v[0-9]+]], 0x42c80000
; GCN: global_atomic_add_f32 v{{\[}}[[LO]]:[[HI]]{{\]}}, [[K]], off offset:512
; GCN: global_store_dwordx2 v{{\[[0-9]+:[0-9]+\]}}, v{{\[}}[[EXTRA_LO]]:[[EXTRA_HI]]{{\]}}
define void @shl_base_global_ptr_global_atomic_fadd(i32 addrspace(1)* %out, i64 addrspace(1)* %extra.use, [512 x i32] addrspace(1)* %ptr) #0 {
  %arrayidx0 = getelementptr inbounds [512 x i32], [512 x i32] addrspace(1)* %ptr, i64 0, i64 32
  %cast = ptrtoint i32 addrspace(1)* %arrayidx0 to i64
  %shl = shl i64 %cast, 2
  %castback = inttoptr i64 %shl to float addrspace(1)*
  call void @llvm.amdgcn.global.atomic.fadd.p1f32.f32(float addrspace(1)* %castback, float 100.0)
  store volatile i64 %cast, i64 addrspace(1)* %extra.use, align 4
  ret void
}

declare void @llvm.amdgcn.global.atomic.fadd.p1f32.f32(float addrspace(1)* nocapture, float) #1

attributes #0 = { nounwind }
attributes #1 = { argmemonly nounwind willreturn }
