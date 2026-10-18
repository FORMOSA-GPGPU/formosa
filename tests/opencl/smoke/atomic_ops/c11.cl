// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#pragma OPENCL EXTENSION cl_khr_int64_base_atomics : enable
#pragma OPENCL EXTENSION cl_khr_int64_extended_atomics : enable
#pragma OPENCL EXTENSION cl_khr_fp64 : enable

#define ORDER memory_order_relaxed
#define SCOPE memory_scope_work_group
#define CHECK(expr) errors = (errors << 1) | !(expr)

#if __OPENCL_C_VERSION__ == 200 || defined(__opencl_c_atomic_order_seq_cst)
#define CHECK_DEFAULTS(T)                                      \
  atomic_store(p, (T)7);                                       \
  CHECK(atomic_load(p) == (T)7);                               \
  CHECK(atomic_exchange(p, (T)8) == (T)7);                     \
  expected = (T)8;                                             \
  CHECK(atomic_compare_exchange_strong(p, &expected, (T)9));   \
  expected = (T)9;                                             \
  while (!atomic_compare_exchange_weak(p, &expected, (T)10)) { \
  }                                                            \
  CHECK(atomic_load(p) == (T)10);
#else
#define CHECK_DEFAULTS(T)
#endif

#define DEFINE_COMMON(T, AT, AS)                                               \
  static uint __attribute__((overloadable)) check_common(volatile AS AT *p) {  \
    uint errors = 0;                                                           \
    T expected;                                                                \
    atomic_init(p, (T)17);                                                     \
    CHECK(atomic_load_explicit(p, ORDER, SCOPE) == (T)17);                     \
    atomic_store_explicit(p, (T)23, ORDER, SCOPE);                             \
    CHECK(atomic_exchange_explicit(p, (T)31, ORDER, SCOPE) == (T)23);          \
    expected = (T)31;                                                          \
    CHECK(atomic_compare_exchange_strong_explicit(p, &expected, (T)41, ORDER,  \
                                                  ORDER, SCOPE));              \
    CHECK(expected == (T)31);                                                  \
    expected = (T)32;                                                          \
    CHECK(!atomic_compare_exchange_strong_explicit(p, &expected, (T)99, ORDER, \
                                                   ORDER, SCOPE));             \
    CHECK(expected == (T)41);                                                  \
    CHECK(atomic_load_explicit(p, ORDER, SCOPE) == (T)41);                     \
    while (!atomic_compare_exchange_weak_explicit(p, &expected, (T)53, ORDER,  \
                                                  ORDER, SCOPE)) {             \
    }                                                                          \
    CHECK(atomic_load_explicit(p, ORDER, SCOPE) == (T)53);                     \
    CHECK_DEFAULTS(T)                                                          \
    return errors;                                                             \
  }

#define DEFINE_INTEGER(T, AT, AS)                                              \
  DEFINE_COMMON(T, AT, AS)                                                     \
  static uint __attribute__((overloadable)) check_integer(volatile AS AT *p) { \
    uint errors = check_common(p);                                             \
    atomic_store_explicit(p, (T)7, ORDER, SCOPE);                              \
    CHECK(atomic_fetch_add_explicit(p, (T)5, ORDER, SCOPE) == (T)7);           \
    CHECK(atomic_fetch_sub_explicit(p, (T)3, ORDER, SCOPE) == (T)12);          \
    CHECK(atomic_fetch_or_explicit(p, (T)16, ORDER, SCOPE) == (T)9);           \
    CHECK(atomic_fetch_xor_explicit(p, (T)3, ORDER, SCOPE) == (T)25);          \
    CHECK(atomic_fetch_and_explicit(p, (T)31, ORDER, SCOPE) == (T)26);         \
    CHECK(atomic_fetch_min_explicit(p, (T)10, ORDER, SCOPE) == (T)26);         \
    CHECK(atomic_fetch_max_explicit(p, (T)20, ORDER, SCOPE) == (T)10);         \
    CHECK(atomic_load_explicit(p, ORDER, SCOPE) == (T)20);                     \
    return errors;                                                             \
  }

#define DEFINE_ADDRESS_SPACE(AS)                                \
  DEFINE_INTEGER(int, atomic_int, AS)                           \
  DEFINE_INTEGER(uint, atomic_uint, AS)                         \
  DEFINE_INTEGER(long, atomic_long, AS)                         \
  DEFINE_INTEGER(ulong, atomic_ulong, AS)                       \
  DEFINE_COMMON(float, atomic_float, AS)                        \
  DEFINE_COMMON(double, atomic_double, AS)                      \
  static uint __attribute__((overloadable)) check_flag(         \
      volatile AS atomic_flag *p) {                             \
    uint errors = 0;                                            \
    atomic_flag_clear_explicit(p, ORDER, SCOPE);                \
    CHECK(!atomic_flag_test_and_set_explicit(p, ORDER, SCOPE)); \
    CHECK(atomic_flag_test_and_set_explicit(p, ORDER, SCOPE));  \
    atomic_flag_clear_explicit(p, ORDER, SCOPE);                \
    CHECK(!atomic_flag_test_and_set_explicit(p, ORDER, SCOPE)); \
    return errors;                                              \
  }

DEFINE_ADDRESS_SPACE(__global)
DEFINE_ADDRESS_SPACE(__local)
#ifdef __opencl_c_generic_address_space
DEFINE_ADDRESS_SPACE(__generic)
#endif

#ifdef __opencl_c_generic_address_space
#define CHECK_GENERIC(FN, AT) \
  errors += FN((__generic AT *)global_value) + FN((__generic AT *)&local_value)
#else
#define CHECK_GENERIC(FN, AT)
#endif

// Keep each scalar case in its own kernel so the test fits the default 1 KiB
// per-thread stack even after the Formosa compiler flattens helper functions.
#define DEFINE_CASE(NAME, T, AT, FN)                                      \
  __kernel void NAME(__global ulong *storage, __global uint *result,      \
                     __global uint *old_values) {                         \
    uint lid = get_local_id(0);                                           \
    uint group = get_group_id(0);                                         \
    result += 3 * group;                                                  \
    old_values += 2 * get_local_size(0) * group;                          \
    __local AT local_value;                                               \
    __global AT *global_value = (__global AT *)storage + group;           \
    if (lid == 0) {                                                       \
      uint errors = FN(global_value) + FN(&local_value);                  \
      CHECK_GENERIC(FN, AT);                                              \
      result[0] = errors;                                                 \
      atomic_store_explicit(global_value, (T)0, ORDER, SCOPE);            \
      atomic_store_explicit(&local_value, (T)0, ORDER, SCOPE);            \
    }                                                                     \
    barrier(CLK_GLOBAL_MEM_FENCE | CLK_LOCAL_MEM_FENCE);                  \
    T expected = atomic_load_explicit(global_value, ORDER, SCOPE);        \
    while (!atomic_compare_exchange_weak_explicit(                        \
        global_value, &expected, expected + (T)1, ORDER, ORDER, SCOPE)) { \
    }                                                                     \
    old_values[lid] = (uint)expected;                                     \
    expected = atomic_load_explicit(&local_value, ORDER, SCOPE);          \
    while (!atomic_compare_exchange_strong_explicit(                      \
        &local_value, &expected, expected + (T)1, ORDER, ORDER, SCOPE)) { \
    }                                                                     \
    old_values[get_local_size(0) + lid] = (uint)expected;                 \
    atomic_work_item_fence(CLK_GLOBAL_MEM_FENCE | CLK_LOCAL_MEM_FENCE,    \
                           memory_order_acq_rel, SCOPE);                  \
    barrier(CLK_GLOBAL_MEM_FENCE | CLK_LOCAL_MEM_FENCE);                  \
    if (lid == 0) {                                                       \
      result[1] = (uint)atomic_load_explicit(global_value, ORDER, SCOPE); \
      result[2] = (uint)atomic_load_explicit(&local_value, ORDER, SCOPE); \
    }                                                                     \
  }

DEFINE_CASE(c11_i32, int, atomic_int, check_integer)
DEFINE_CASE(c11_u32, uint, atomic_uint, check_integer)
DEFINE_CASE(c11_i64, long, atomic_long, check_integer)
DEFINE_CASE(c11_u64, ulong, atomic_ulong, check_integer)
DEFINE_CASE(c11_f32, float, atomic_float, check_common)
DEFINE_CASE(c11_f64, double, atomic_double, check_common)

__kernel void c11_flag(__global ulong *storage, __global uint *result) {
  __local atomic_flag local_value;
  __global atomic_flag *global_value = (__global atomic_flag *)storage;
  uint errors = check_flag(global_value) + check_flag(&local_value);
  CHECK_GENERIC(check_flag, atomic_flag);
  result[0] = errors;
}
