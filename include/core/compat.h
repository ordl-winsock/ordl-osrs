/*
 * ORDL GovCon - C23 Compatibility Layer
 * For compilers that do not yet support full C23
 */

#ifndef GOVCON_COMPAT_H
#define GOVCON_COMPAT_H

#include <stddef.h>

/* nullptr: C23 keyword, fallback for pre-C23 compilers */
#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 202311L)
#undef nullptr
#define nullptr ((void *)0)
#endif

/* static_assert: C11 has _Static_assert, C23 has static_assert keyword */
#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 202311L)
#undef static_assert
#define static_assert _Static_assert
#endif

/* typeof: GNU extension, standardized in C23 */
#ifndef typeof
#define typeof __typeof__
#endif

/* thread_local: C11 _Thread_local, C23 thread_local keyword.
 * Only define for pre-C23, where thread_local is not already a keyword. */
#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 202311L)
#ifndef thread_local
#define thread_local _Thread_local
#endif
#endif

/* constexpr: C23 keyword. Pre-C23: use const (loses compile-time semantics). */
#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 202311L)
#ifndef constexpr
#define constexpr const
#endif
#endif

/* nodiscard: C23 attribute. Pre-C23: use GCC/Clang warn_unused_result. */
#if !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 202311L)
#define nodiscard __attribute__((warn_unused_result))
#else
#define nodiscard [[nodiscard]]
#endif

/* Project namespaced alias */
#define GC_NODISCARD

/* Helper to discard a nodiscard return value without warning.
 * GCC < 12 does not suppress warn_unused_result via (void) cast.
 * Uses a statement expression (GNU C extension) so it works in any context.
 * __LINE__ ensures unique variable names to avoid shadow warnings.
 */
#define GC_DISCARD(expr)                                                       \
  ((__extension__({                                                            \
    __typeof__(expr) _gc_discard_##__LINE__ __attribute__((unused)) = (expr);  \
    _gc_discard_##__LINE__;                                                    \
  })))

/* bool/true/false: C99 <stdbool.h> */
#include <stdbool.h>

/* ISO C forbids empty translation units */
typedef int govcon_compat_dummy_t;

#endif /* GOVCON_COMPAT_H */
