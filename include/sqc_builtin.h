/**
 * @file sqc_builtin.h
 * @brief Standard Built-in Primitives for SQC (Static Quality & Hazard Analyzer)
 */
#ifndef SQC_BUILTIN_H
#define SQC_BUILTIN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Function override annotation for test drivers and peripheral/RTOS mocks */
#define SQC_OVERRIDE __attribute__((annotate("sqc_override"))) __attribute__((weak))
#define sqc_override annotate("sqc_override"), weak

/* Core Symbolic Interval Primitives */
long long __sqc_interval_i64(long long min_val, long long max_val);
unsigned long long __sqc_interval_u64(unsigned long long min_val, unsigned long long max_val);
long long __sqc_interval_split_i64(long long min_val, long long max_val);

#define sqc_interval(min_v, max_v)              ((int)__sqc_interval_i64((long long)(min_v), (long long)(max_v)))
#define sqc_int_interval(min_v, max_v)          ((int)__sqc_interval_i64((long long)(min_v), (long long)(max_v)))
#define sqc_long_long_interval(min_v, max_v)    (__sqc_interval_i64((long long)(min_v), (long long)(max_v)))
#define sqc_unsigned_char_interval(min_v, max_v)((unsigned char)__sqc_interval_u64((unsigned long long)(min_v), (unsigned long long)(max_v)))
#define sqc_unsigned_int_interval(min_v, max_v) ((unsigned int)__sqc_interval_u64((unsigned long long)(min_v), (unsigned long long)(max_v)))
#define sqc_unsigned_long_interval(min_v, max_v)((unsigned long)__sqc_interval_u64((unsigned long long)(min_v), (unsigned long long)(max_v)))
#define sqc_interval_split(min_v, max_v)        ((int)__sqc_interval_split_i64((long long)(min_v), (long long)(max_v)))

/* Symbolic Memory & String Initialization */
void __sqc_make_unknown(void *ptr, size_t size);
void __sqc_make_cstring(char *ptr, size_t max_size);

#define sqc_make_unknown(ptr, size)             __sqc_make_unknown((void *)(ptr), (size_t)(size))
#define sqc_make_cstring(ptr, max_size)         __sqc_make_cstring((char *)(ptr), (size_t)(max_size))

/* Nondeterministic Choice Primitives */
long long __sqc_nondet_i64(long long a, long long b);
void *__sqc_nondet_ptr2(const void *p1, const void *p2);
void *__sqc_nondet_ptr3(const void *p1, const void *p2, const void *p3);
void *__sqc_nondet_ptr5(const void *p1, const void *p2, const void *p3, const void *p4, const void *p5);

#define sqc_nondet(a, b)                        (__sqc_nondet_i64((long long)(a), (long long)(b)))
#define sqc_nondet_ptr(p1, p2)                  (__sqc_nondet_ptr2((const void *)(p1), (const void *)(p2)))
#define sqc_nondet3_ptr(p1, p2, p3)             (__sqc_nondet_ptr3((const void *)(p1), (const void *)(p2), (const void *)(p3)))
#define sqc_nondet5_ptr(p1, p2, p3, p4, p5)     (__sqc_nondet_ptr5((const void *)(p1), (const void *)(p2), (const void *)(p3), (const void *)(p4), (const void *)(p5)))

/* White-box Static Variable Lookup */
void *__sqc_find_variable(const char *var_name, const char *file_name);
#define sqc_find_variable(var_name, file_name)  (__sqc_find_variable((var_name), (file_name)))

/* Assertions, Assumptions & Custom UB Specifications */
void __sqc_assert(bool cond, const char *expr_str, const char *file, int line);
void __sqc_assume(bool cond);
void __sqc_ub(const char *msg, long long trigger);
void __sqc_requires_valid(const void *ptr, size_t byte_len, const char *file, int line);

#define sqc_assert(cond)                        __sqc_assert((bool)(cond), #cond, __FILE__, __LINE__)
#define sqc_assume(cond)                        __sqc_assume((bool)(cond))
#define sqc_ub(msg, trigger)                    __sqc_ub((msg), (long long)(trigger))
#define sqc_requires_valid(ptr, byte_len)       __sqc_requires_valid((const void *)(ptr), (size_t)(byte_len), __FILE__, __LINE__)
#define sqc_requires_valid_read(ptr, byte_len)  __sqc_requires_valid((const void *)(ptr), (size_t)(byte_len), __FILE__, __LINE__)

extern volatile long long sqc_entropy_source;

#ifdef __cplusplus
}
#endif

#endif /* SQC_BUILTIN_H */
