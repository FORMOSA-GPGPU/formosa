// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma OPENCL EXTENSION cl_khr_int64_base_atomics : enable
#pragma OPENCL EXTENSION cl_khr_int64_extended_atomics : enable

#define OP_COUNT 10
#define OP_ADD 0
#define OP_SUB 1
#define OP_INC 2
#define OP_DEC 3
#define OP_AND 4
#define OP_OR 5
#define OP_XOR 6
#define OP_XCHG 7
#define OP_MIN 8
#define OP_MAX 9
#define LOCAL_OP_STRIDE 64

#define DEFINE_GLOBAL_OPS_KERNEL(NAME, TYPE, FN_ADD, FN_SUB, FN_INC, FN_DEC,   \
                                 FN_AND, FN_OR, FN_XOR, FN_XCHG, FN_MIN,       \
                                 FN_MAX)                                        \
  __kernel void NAME(volatile __global TYPE *values,                            \
                     __global const TYPE *operands,                             \
                     __global TYPE *old_values)                                 \
  {                                                                             \
    size_t gid = get_global_id(0);                                              \
    size_t stride = get_global_size(0);                                         \
    size_t idx = OP_ADD * stride + gid;                                         \
    old_values[idx] = FN_ADD(&values[idx], operands[idx]);                      \
    idx = OP_SUB * stride + gid;                                                \
    old_values[idx] = FN_SUB(&values[idx], operands[idx]);                      \
    idx = OP_INC * stride + gid;                                                \
    old_values[idx] = FN_INC(&values[idx]);                                     \
    idx = OP_DEC * stride + gid;                                                \
    old_values[idx] = FN_DEC(&values[idx]);                                     \
    idx = OP_AND * stride + gid;                                                \
    old_values[idx] = FN_AND(&values[idx], operands[idx]);                      \
    idx = OP_OR * stride + gid;                                                 \
    old_values[idx] = FN_OR(&values[idx], operands[idx]);                       \
    idx = OP_XOR * stride + gid;                                                \
    old_values[idx] = FN_XOR(&values[idx], operands[idx]);                      \
    idx = OP_XCHG * stride + gid;                                               \
    old_values[idx] = FN_XCHG(&values[idx], operands[idx]);                     \
    idx = OP_MIN * stride + gid;                                                \
    old_values[idx] = FN_MIN(&values[idx], operands[idx]);                      \
    idx = OP_MAX * stride + gid;                                                \
    old_values[idx] = FN_MAX(&values[idx], operands[idx]);                      \
  }

#define DEFINE_CONTENDED_GLOBAL_ADD_KERNEL(NAME, TYPE, FN_ADD)                  \
  __kernel void NAME(volatile __global TYPE *counter, __global TYPE *old_values)\
  {                                                                             \
    size_t gid = get_global_id(0);                                              \
    old_values[gid] = FN_ADD(counter, (TYPE)1);                                 \
  }

#define DEFINE_LOCAL_OPS_KERNEL(NAME, TYPE, FN_ADD, FN_SUB, FN_INC, FN_DEC,    \
                                FN_AND, FN_OR, FN_XOR, FN_XCHG, FN_MIN,        \
                                FN_MAX)                                         \
  __kernel void NAME(__global const TYPE *initials,                             \
                     __global const TYPE *operands,                             \
                     __global TYPE *final_values,                               \
                     __global TYPE *old_values)                                 \
  {                                                                             \
    const size_t lid = get_local_id(0);                                         \
    const size_t stride = get_local_size(0);                                    \
    __local TYPE values[OP_COUNT * LOCAL_OP_STRIDE];                            \
    size_t idx = OP_ADD * stride + lid;                                         \
    values[idx] = initials[idx];                                                \
    idx = OP_SUB * stride + lid;                                                \
    values[idx] = initials[idx];                                                \
    idx = OP_INC * stride + lid;                                                \
    values[idx] = initials[idx];                                                \
    idx = OP_DEC * stride + lid;                                                \
    values[idx] = initials[idx];                                                \
    idx = OP_AND * stride + lid;                                                \
    values[idx] = initials[idx];                                                \
    idx = OP_OR * stride + lid;                                                 \
    values[idx] = initials[idx];                                                \
    idx = OP_XOR * stride + lid;                                                \
    values[idx] = initials[idx];                                                \
    idx = OP_XCHG * stride + lid;                                               \
    values[idx] = initials[idx];                                                \
    idx = OP_MIN * stride + lid;                                                \
    values[idx] = initials[idx];                                                \
    idx = OP_MAX * stride + lid;                                                \
    values[idx] = initials[idx];                                                \
    barrier(CLK_LOCAL_MEM_FENCE);                                               \
    idx = OP_ADD * stride + lid;                                                \
    old_values[idx] = FN_ADD(&values[idx], operands[idx]);                      \
    idx = OP_SUB * stride + lid;                                                \
    old_values[idx] = FN_SUB(&values[idx], operands[idx]);                      \
    idx = OP_INC * stride + lid;                                                \
    old_values[idx] = FN_INC(&values[idx]);                                     \
    idx = OP_DEC * stride + lid;                                                \
    old_values[idx] = FN_DEC(&values[idx]);                                     \
    idx = OP_AND * stride + lid;                                                \
    old_values[idx] = FN_AND(&values[idx], operands[idx]);                      \
    idx = OP_OR * stride + lid;                                                 \
    old_values[idx] = FN_OR(&values[idx], operands[idx]);                       \
    idx = OP_XOR * stride + lid;                                                \
    old_values[idx] = FN_XOR(&values[idx], operands[idx]);                      \
    idx = OP_XCHG * stride + lid;                                               \
    old_values[idx] = FN_XCHG(&values[idx], operands[idx]);                     \
    idx = OP_MIN * stride + lid;                                                \
    old_values[idx] = FN_MIN(&values[idx], operands[idx]);                      \
    idx = OP_MAX * stride + lid;                                                \
    old_values[idx] = FN_MAX(&values[idx], operands[idx]);                      \
    barrier(CLK_LOCAL_MEM_FENCE);                                               \
    idx = OP_ADD * stride + lid;                                                \
    final_values[idx] = values[idx];                                            \
    idx = OP_SUB * stride + lid;                                                \
    final_values[idx] = values[idx];                                            \
    idx = OP_INC * stride + lid;                                                \
    final_values[idx] = values[idx];                                            \
    idx = OP_DEC * stride + lid;                                                \
    final_values[idx] = values[idx];                                            \
    idx = OP_AND * stride + lid;                                                \
    final_values[idx] = values[idx];                                            \
    idx = OP_OR * stride + lid;                                                 \
    final_values[idx] = values[idx];                                            \
    idx = OP_XOR * stride + lid;                                                \
    final_values[idx] = values[idx];                                            \
    idx = OP_XCHG * stride + lid;                                               \
    final_values[idx] = values[idx];                                            \
    idx = OP_MIN * stride + lid;                                                \
    final_values[idx] = values[idx];                                            \
    idx = OP_MAX * stride + lid;                                                \
    final_values[idx] = values[idx];                                            \
  }

#define DEFINE_CONTENDED_LOCAL_ADD_KERNEL(NAME, TYPE, FN_ADD)                   \
  __kernel void NAME(__global TYPE *final_counter, __global TYPE *old_values)   \
  {                                                                             \
    const size_t lid = get_local_id(0);                                         \
    __local TYPE counter;                                                       \
    if (lid == 0) {                                                             \
      counter = (TYPE)0;                                                        \
    }                                                                           \
    barrier(CLK_LOCAL_MEM_FENCE);                                               \
    old_values[lid] = FN_ADD(&counter, (TYPE)1);                                \
    barrier(CLK_LOCAL_MEM_FENCE);                                               \
    if (lid == 0) {                                                             \
      final_counter[0] = counter;                                               \
    }                                                                           \
  }

DEFINE_GLOBAL_OPS_KERNEL(atomic_ops_global_i32, int, atomic_add, atomic_sub,
                         atomic_inc, atomic_dec, atomic_and, atomic_or,
                         atomic_xor, atomic_xchg, atomic_min, atomic_max)
DEFINE_GLOBAL_OPS_KERNEL(atomic_ops_global_u32, uint, atomic_add, atomic_sub,
                         atomic_inc, atomic_dec, atomic_and, atomic_or,
                         atomic_xor, atomic_xchg, atomic_min, atomic_max)
DEFINE_GLOBAL_OPS_KERNEL(atomic_ops_global_i64, long, atom_add, atom_sub,
                         atom_inc, atom_dec, atom_and, atom_or, atom_xor,
                         atom_xchg, atom_min, atom_max)
DEFINE_GLOBAL_OPS_KERNEL(atomic_ops_global_u64, ulong, atom_add, atom_sub,
                         atom_inc, atom_dec, atom_and, atom_or, atom_xor,
                         atom_xchg, atom_min, atom_max)

DEFINE_CONTENDED_GLOBAL_ADD_KERNEL(atomic_contended_global_u32, uint,
                                   atomic_add)
DEFINE_CONTENDED_GLOBAL_ADD_KERNEL(atomic_contended_global_u64, ulong,
                                   atom_add)
DEFINE_LOCAL_OPS_KERNEL(atomic_ops_local_i32, int, atomic_add, atomic_sub,
                        atomic_inc, atomic_dec, atomic_and, atomic_or,
                        atomic_xor, atomic_xchg, atomic_min, atomic_max)
DEFINE_LOCAL_OPS_KERNEL(atomic_ops_local_u32, uint, atomic_add, atomic_sub,
                        atomic_inc, atomic_dec, atomic_and, atomic_or,
                        atomic_xor, atomic_xchg, atomic_min, atomic_max)
DEFINE_LOCAL_OPS_KERNEL(atomic_ops_local_i64, long, atom_add, atom_sub,
                        atom_inc, atom_dec, atom_and, atom_or, atom_xor,
                        atom_xchg, atom_min, atom_max)
DEFINE_LOCAL_OPS_KERNEL(atomic_ops_local_u64, ulong, atom_add, atom_sub,
                        atom_inc, atom_dec, atom_and, atom_or, atom_xor,
                        atom_xchg, atom_min, atom_max)
DEFINE_CONTENDED_LOCAL_ADD_KERNEL(atomic_contended_local_u32, uint, atomic_add)
DEFINE_CONTENDED_LOCAL_ADD_KERNEL(atomic_contended_local_u64, ulong, atom_add)

#undef DEFINE_CONTENDED_LOCAL_ADD_KERNEL
#undef DEFINE_LOCAL_OPS_KERNEL
#undef DEFINE_CONTENDED_GLOBAL_ADD_KERNEL
#undef DEFINE_GLOBAL_OPS_KERNEL
