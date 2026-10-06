/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


#ifndef KOSKARI_OPERATOR_H
#define KOSKARI_OPERATOR_H


#include <Python.h>
#include <limits.h>
#include <math.h>

#include "bytecode.h"
#include "hints.h"


/* === Truthiness === */


/* Zero test for an exact int without the general conversion: a compact
   int reads its one digit, and an int that is not compact is nonzero
   by construction (3.12 and later); before 3.12 the size word is zero
   exactly for zero. */
static inline int kos_long_is_zero(PyObject *v) {
#if PY_VERSION_HEX >= 0x030C0000
  return PyUnstable_Long_IsCompact((PyLongObject *) v)
      && PyUnstable_Long_CompactValue((PyLongObject *) v) == 0;
#else
  return Py_SIZE(v) == 0;
#endif
}


/* PyObject_IsTrue with the common cases inline: the two bools, None,
   and an exact int. Same contract: 1, 0, or -1 with an error set. */
static inline int kos_object_truth(PyObject *v) {
  if (v == Py_True)
    return 1;
  if (v == Py_False || v == Py_None)
    return 0;
  if (PyLong_CheckExact(v))
    return ! kos_long_is_zero(v);
  return PyObject_IsTrue(v);
}


/* PyObject_Not with the same inline cases. */
static inline int kos_object_not(PyObject *v) {
  if (v == Py_True)
    return 0;
  if (v == Py_False || v == Py_None)
    return 1;
  if (PyLong_CheckExact(v))
    return kos_long_is_zero(v);
  return PyObject_Not(v);
}


/* === PyLong two-operand fast path === */

/* The digit layout of an exact int, read inline. 3.12 and later keep
   the digit count and the sign in the tag word: the low two bits are
   the sign (0 positive, 1 zero, 2 negative) and the count sits above
   three flag bits. Before 3.12 the size word is the signed digit
   count. Only builds with 30-bit digits take the inline read; a 15-bit
   digit build goes through the conversion below. */
#if PyLong_SHIFT == 30
# define KOS_LONG_INLINE_DIGITS 1
# if PY_VERSION_HEX >= 0x030C0000
#  define KOS_LONG_NDIGITS(lo) \
   ((size_t) ((lo)->long_value.lv_tag >> _PyLong_NON_SIZE_BITS))
#  define KOS_LONG_NEGATIVE(lo) \
   (((lo)->long_value.lv_tag & _PyLong_SIGN_MASK) == 2)
#  define KOS_LONG_DIGITS(lo)   ((lo)->long_value.ob_digit)
# else
#  define KOS_LONG_NDIGITS(lo) \
   ((size_t) (Py_SIZE(lo) < 0 ? -Py_SIZE(lo) : Py_SIZE(lo)))
#  define KOS_LONG_NEGATIVE(lo) (Py_SIZE(lo) < 0)
#  define KOS_LONG_DIGITS(lo)   ((lo)->ob_digit)
# endif
#else
# define KOS_LONG_INLINE_DIGITS 0
#endif

/* An int result may be written into an operand the caller owns
   outright instead of allocated, where the digit layout is known and
   refcounts are exact (a free-threaded build biases them). */
#if KOS_LONG_INLINE_DIGITS && ! defined(Py_GIL_DISABLED)
# define KOS_LONG_REUSE 1
#else
# define KOS_LONG_REUSE 0
#endif

/* A float result may likewise be written into an exact float the
   caller owns outright; there is no cached float to keep the identity
   of, so only the refcount rule applies. */
#if ! defined(Py_GIL_DISABLED)
# define KOS_FLOAT_REUSE 1
#else
# define KOS_FLOAT_REUSE 0
#endif


/* Read an exact int straight from its digits. Two 30-bit digits are at
   most 60 bits and always fit. A third digit fits when it is below 8,
   which puts the magnitude under 2^63; the one representable value
   that misses, -2^63, declines like any wider int. Returns 1 with *out
   set, or 0 when the value does not fit a long long; no conversion
   call is made either way, so a bignum's decline is a tag-word read.
   A build without the inline digit layout returns -1, and the caller
   converts the general way. */
KOS_OP_INLINE int kos_long_as_ll_digits(PyObject *v, long long *out) {
#if KOS_LONG_INLINE_DIGITS
  PyLongObject *lo = (PyLongObject *) v;
  const digit *digits = KOS_LONG_DIGITS(lo);
  long long mag;

  switch (KOS_LONG_NDIGITS(lo)) {
    case 0:
      mag = 0;
      break;
    case 1:
      mag = digits[0];
      break;
    case 2:
      mag = digits[0] | ((long long) digits[1] << PyLong_SHIFT);
      break;
    case 3:
      if (digits[2] >= (digit) (1u << (63 - 2 * PyLong_SHIFT)))
        return 0;
      mag = digits[0] | ((long long) digits[1] << PyLong_SHIFT)
          | ((long long) digits[2] << (2 * PyLong_SHIFT));
      break;
    default:
      return 0;
  }

  *out = KOS_LONG_NEGATIVE(lo) ? -mag : mag;
  return 1;
#else
  (void) v;
  (void) out;
  return -1;
#endif
}


/* An exact int as a C long long: the digit read decides on a build
   with the inline layout, and the general conversion decides elsewhere.
   Returns 1 with *out set, or 0 when the value does not fit. */
KOS_OP_INLINE int kos_long_as_ll(PyObject *v, long long *out) {
  int r = kos_long_as_ll_digits(v, out);
  if (likely(r >= 0))
    return r;

  int overflow;
  *out = PyLong_AsLongLongAndOverflow(v, &overflow);
  return ! overflow;
}


/* Both operands exact ints that fit in a C long long.  Mechanical extraction
   only, with no operator knowledge, so it never re-branches on op.  Returns 1
   with both *a and *b set, or 0 when either operand declines the fast path. */
KOS_OP_INLINE int kos_as_ll2(
    PyObject *l,
    PyObject *r,
    long long *a,
    long long *b) {

  if (! (PyLong_CheckExact(l) && PyLong_CheckExact(r)))
    return 0;
  if (unlikely(! kos_long_as_ll(l, a)) || unlikely(! kos_long_as_ll(r, b)))
    return 0;
  return 1;
}


/* Per-operator two-operand PyLong fast paths.  Each is self-contained and
   written out explicitly -- no shared operator switch -- so a caller that
   already knows its operator lands straight in the one arithmetic it needs.
   When both operands are exact ints and the operation cannot lose precision,
   compute it directly and skip the PyNumber slot dispatch and longobject digit
   loops.

   Each operator has two forms. The _ll form takes the two C values a
   caller has already extracted (an elected int body, which guarded the
   types itself) and an operand the caller owns outright that the
   result may be written into, or NULL; the object form runs kos_as_ll2
   and then the _ll form with the same candidate, for the operator
   cores (which offer none) and the reduce fold (which offers its
   accumulator).

   Return convention (all of them): 1 with *out set (new ref) when handled, 0
   when declined (*out untouched), -1 with an error set on allocation failure
   (*out NULL).  Bitwise AND/OR/XOR on two's-complement match Python int
   semantics for any in-range operands.  Floor division and remainder
   round toward negative infinity as Python does; a zero divisor declines
   so the generic path raises.  Shifts take a negative left operand
   through its magnitude (left) or as a floor shift (right); a negative
   count declines so the generic path raises.  True division of two ints
   within 53 bits is the correctly rounded double, which is what
   int.__truediv__ computes for them.  Power takes a non-negative
   exponent by squaring; a negative one is a float and declines.
   Overflow on +, -, *, <<, ** declines rather than widening.  Matrix
   multiply has no fast path. */

/* The digits an int of magnitude mag needs; the caller's values are at
   most 63 bits, so three at most. */
KOS_OP_INLINE size_t kos_long_digits_needed(unsigned long long mag) {
  return 1 + (mag >> PyLong_SHIFT != 0) + (mag >> (2 * PyLong_SHIFT) != 0);
}


#if KOS_LONG_REUSE
/* Write res into v, an exact int the caller owns outright with at
   least the digits res needs. The digits are written low to high and
   the tag last, its reserved flag bit kept. */
KOS_OP_INLINE void kos_long_write(PyObject *v, long long res) {
  PyLongObject *lo = (PyLongObject *) v;
  digit *digits = KOS_LONG_DIGITS(lo);
  unsigned long long mag = res < 0 ? 0ull - (unsigned long long) res
                                   : (unsigned long long) res;
  size_t n = kos_long_digits_needed(mag);

  digits[0] = (digit) (mag & PyLong_MASK);
  if (n > 1)
    digits[1] = (digit) ((mag >> PyLong_SHIFT) & PyLong_MASK);
  if (n > 2)
    digits[2] = (digit) (mag >> (2 * PyLong_SHIFT));
# if PY_VERSION_HEX >= 0x030C0000
  {
    uintptr_t flags = lo->long_value.lv_tag
        & ((((uintptr_t) 1) << _PyLong_NON_SIZE_BITS) - 1)
        & ~((uintptr_t) _PyLong_SIGN_MASK);
    lo->long_value.lv_tag = (((uintptr_t) n) << _PyLong_NON_SIZE_BITS)
        | flags | (res < 0 ? 2u : 0u);
  }
# else
  Py_SET_SIZE(lo, res < 0 ? -(Py_ssize_t) n : (Py_ssize_t) n);
# endif
}


/* Whether res can be written into v in place of an allocation: v is
   an exact int (the caller checked) whose only reference is the
   caller's, res lies outside the small-int range so the identity of
   the cached ints stays CPython's, and res needs no more digits than
   v holds. A small int is immortal on 3.12 and later and shared by the
   cache before, so the refcount test alone excludes it as a target. */
KOS_OP_INLINE int kos_long_reusable(PyObject *v, long long res) {
  unsigned long long mag;

  if (Py_REFCNT(v) != 1 || (res >= -5 && res <= 256))
    return 0;
  mag = res < 0 ? 0ull - (unsigned long long) res : (unsigned long long) res;
  return kos_long_digits_needed(mag) <= KOS_LONG_NDIGITS((PyLongObject *) v);
}
#endif


/* Materialise res: into reuse, else into reuse2, when the caller
   offered an operand it owns outright and kos_long_reusable allows it,
   in which case *out is that same object and the caller's reference to
   it becomes the result reference; else a fresh int. The two
   candidates are a pair body's left and right operands: whichever is
   dying serves, the left first. Either may be NULL. Returns 1, or -1
   with an error set. */
KOS_OP_INLINE int kos_long_fast_result(
    long long res, PyObject *reuse, PyObject *reuse2, PyObject **out) {

#if KOS_LONG_REUSE
  if (reuse && kos_long_reusable(reuse, res)) {
    kos_long_write(reuse, res);
    *out = reuse;
    return 1;
  }
  if (reuse2 && kos_long_reusable(reuse2, res)) {
    kos_long_write(reuse2, res);
    *out = reuse2;
    return 1;
  }
#else
  (void) reuse;
  (void) reuse2;
#endif
  *out = PyLong_FromLongLong(res);
  return likely(*out != NULL) ? 1 : -1;
}


/* The arithmetic of each int operator as a step over two long longs:
   1 with *res set, 0 for a decline (overflow, a zero divisor, a
   negative shift), nothing allocated. The _ll forms below materialise
   one step; a fold runs the steps in C and materialises once. */
KOS_OP_INLINE int kos_ll_add(long long a, long long b, long long *res) {
  return ! __builtin_add_overflow(a, b, res);
}


KOS_OP_INLINE int kos_ll_subtract(long long a, long long b, long long *res) {
  return ! __builtin_sub_overflow(a, b, res);
}


KOS_OP_INLINE int kos_ll_multiply(long long a, long long b, long long *res) {
  return ! __builtin_mul_overflow(a, b, res);
}


/* Floor division and remainder with Python's rounding toward negative
   infinity. A zero divisor declines, and so does the one overflowing
   quotient, LLONG_MIN / -1. */
KOS_OP_INLINE int kos_ll_floor_divmod(
    long long a, long long b, long long *q, long long *r) {

  if (unlikely(b == 0 || (a == LLONG_MIN && b == -1)))
    return 0;
  *q = a / b;
  *r = a % b;
  if (*r != 0 && ((*r < 0) != (b < 0))) {
    *q -= 1;
    *r += b;
  }
  return 1;
}


KOS_OP_INLINE int kos_ll_modulo(long long a, long long b, long long *res) {
  long long q;
  return kos_ll_floor_divmod(a, b, &q, res);
}


KOS_OP_INLINE int kos_ll_floor_divide(long long a, long long b, long long *res) {
  long long r;
  return kos_ll_floor_divmod(a, b, res, &r);
}


/* Non-negative exponents by squaring, declining on overflow; a
   negative exponent is a float result and declines. */
KOS_OP_INLINE int kos_ll_power(long long a, long long b, long long *res) {
  long long acc = 1;
  long long base = a;

  if (unlikely(b < 0))
    return 0;
  while (b) {
    if ((b & 1) && unlikely(__builtin_mul_overflow(acc, base, &acc)))
      return 0;
    b >>= 1;
    if (b && unlikely(__builtin_mul_overflow(base, base, &base)))
      return 0;
  }
  *res = acc;
  return 1;
}


KOS_OP_INLINE int kos_ll_bit_and(long long a, long long b, long long *res) {
  *res = a & b;
  return 1;
}


KOS_OP_INLINE int kos_ll_bit_or(long long a, long long b, long long *res) {
  *res = a | b;
  return 1;
}


KOS_OP_INLINE int kos_ll_bit_xor(long long a, long long b, long long *res) {
  *res = a ^ b;
  return 1;
}


/* A negative left operand shifts through its magnitude, so the C shift
   never sees a negative value; LLONG_MIN's magnitude does not fit and
   declines. */
KOS_OP_INLINE int kos_ll_bit_left(long long a, long long b, long long *res) {
  long long mag;

  if (unlikely(b < 0 || b >= 63))
    return 0;
  if (a >= 0) {
    if (unlikely(a > (LLONG_MAX >> b)))
      return 0;
    *res = a << b;
    return 1;
  }
  if (unlikely(a == LLONG_MIN))
    return 0;
  mag = -a;
  if (unlikely(mag > (LLONG_MAX >> b)))
    return 0;
  *res = -(mag << b);
  return 1;
}


/* A negative left operand takes the floor shift Python defines, spelled
   through the complement so no negative value reaches the C shift. */
KOS_OP_INLINE int kos_ll_bit_right(long long a, long long b, long long *res) {
  if (unlikely(b < 0))
    return 0;
  if (b >= 63)
    *res = a < 0 ? -1 : 0;
  else
    *res = a >= 0 ? (a >> b) : ~((~a) >> b);
  return 1;
}


/* The _ll forms: one step, materialised into the first of the two
   reuse candidates that can take it, else a fresh int. */
#define KOS_LONG_FAST_LL_FORM(NAME)                                     \
  KOS_OP_INLINE int long_fast_##NAME##_ll(                              \
      long long a, long long b, PyObject *reuse, PyObject *reuse2,      \
      PyObject **out) {                                                 \
    long long res;                                                      \
    if (unlikely(! kos_ll_##NAME(a, b, &res)))                          \
      return 0;                                                         \
    return kos_long_fast_result(res, reuse, reuse2, out);               \
  }

KOS_LONG_FAST_LL_FORM(add)
KOS_LONG_FAST_LL_FORM(subtract)
KOS_LONG_FAST_LL_FORM(multiply)
KOS_LONG_FAST_LL_FORM(modulo)
KOS_LONG_FAST_LL_FORM(floor_divide)
KOS_LONG_FAST_LL_FORM(power)
KOS_LONG_FAST_LL_FORM(bit_and)
KOS_LONG_FAST_LL_FORM(bit_or)
KOS_LONG_FAST_LL_FORM(bit_xor)
KOS_LONG_FAST_LL_FORM(bit_left)
KOS_LONG_FAST_LL_FORM(bit_right)

#undef KOS_LONG_FAST_LL_FORM


/* Two ints within 53 bits divide exactly as doubles: IEEE division of
   exactly represented values is correctly rounded, which is what
   int.__truediv__ computes for them. Wider operands and a zero divisor
   decline. The result is a float, so there is no int step to fold. */
KOS_OP_INLINE int long_fast_true_divide_ll(
    long long a, long long b, PyObject *reuse, PyObject *reuse2,
    PyObject **out) {
  const long long lim = 1LL << 53;
  (void) reuse;
  (void) reuse2;
  if (unlikely(b == 0 || a <= -lim || a >= lim || b <= -lim || b >= lim))
    return 0;
  *out = PyFloat_FromDouble((double) a / (double) b);
  return likely(*out != NULL) ? 1 : -1;
}


/* The object forms: extract, then the _ll form. reuse is the object
   the result may be written into, as for the _ll form, or NULL. */
#define KOS_LONG_FAST_OBJECT_FORM(NAME)                                 \
  KOS_OP_INLINE int long_fast_##NAME(                                   \
      PyObject *l, PyObject *r, PyObject *reuse, PyObject **out) {      \
    long long a, b;                                                     \
    if (! kos_as_ll2(l, r, &a, &b))                                     \
      return 0;                                                         \
    return long_fast_##NAME##_ll(a, b, reuse, NULL, out);               \
  }

KOS_LONG_FAST_OBJECT_FORM(add)
KOS_LONG_FAST_OBJECT_FORM(subtract)
KOS_LONG_FAST_OBJECT_FORM(multiply)
KOS_LONG_FAST_OBJECT_FORM(modulo)
KOS_LONG_FAST_OBJECT_FORM(bit_and)
KOS_LONG_FAST_OBJECT_FORM(bit_or)
KOS_LONG_FAST_OBJECT_FORM(bit_xor)
KOS_LONG_FAST_OBJECT_FORM(bit_left)
KOS_LONG_FAST_OBJECT_FORM(bit_right)
KOS_LONG_FAST_OBJECT_FORM(floor_divide)
KOS_LONG_FAST_OBJECT_FORM(true_divide)
KOS_LONG_FAST_OBJECT_FORM(power)

#undef KOS_LONG_FAST_OBJECT_FORM


/* === PyFloat two-operand fast paths === */

/* Per-operator two-operand float fast paths, the float analogue of the
   PyLong paths above and shaped the same: a _d form taking the two
   doubles and a candidate the result may be written into, an object
   form that checks both operands are exact floats, and a num form
   that tries the int path and then the float path, for the operator
   cores. The arithmetic is what floatobject.c computes: add, subtract,
   multiply, and true division are the IEEE operation (a zero divisor
   declines, so the generic path raises), remainder and floor division
   are ports of float_rem and float_divmod, so their signs and their
   handling of infinities and NaN match. Power has no float fast path:
   its special cases (a negative base with a fractional exponent gives
   a complex, a zero base with a negative exponent raises) stay with
   the generic path. */

/* Materialise a double result: into reuse, else into reuse2, when the
   caller offered an exact float it owns outright, in which case *out
   is that same object and the caller's reference becomes the result
   reference; else a fresh float. The candidates are a pair body's
   operands, the left first; either may be NULL. Returns 1, or -1 with
   an error set. */
KOS_OP_INLINE int kos_float_fast_result(
    double res, PyObject *reuse, PyObject *reuse2, PyObject **out) {

#if KOS_FLOAT_REUSE
  if (reuse && Py_REFCNT(reuse) == 1) {
    ((PyFloatObject *) reuse)->ob_fval = res;
    *out = reuse;
    return 1;
  }
  if (reuse2 && Py_REFCNT(reuse2) == 1) {
    ((PyFloatObject *) reuse2)->ob_fval = res;
    *out = reuse2;
    return 1;
  }
#else
  (void) reuse;
  (void) reuse2;
#endif
  *out = PyFloat_FromDouble(res);
  return likely(*out != NULL) ? 1 : -1;
}


/* The arithmetic of each float operator as a step over two doubles:
   1 with *res set, 0 for a zero divisor. */
KOS_OP_INLINE int kos_d_add(double a, double b, double *res) {
  *res = a + b;
  return 1;
}


KOS_OP_INLINE int kos_d_subtract(double a, double b, double *res) {
  *res = a - b;
  return 1;
}


KOS_OP_INLINE int kos_d_multiply(double a, double b, double *res) {
  *res = a * b;
  return 1;
}


KOS_OP_INLINE int kos_d_true_divide(double a, double b, double *res) {
  if (unlikely(b == 0.0))
    return 0;
  *res = a / b;
  return 1;
}


/* Python's float remainder: the sign of the divisor, a zero result
   carrying the divisor's sign. */
KOS_OP_INLINE int kos_d_modulo(double a, double b, double *res) {
  double mod;

  if (unlikely(b == 0.0))
    return 0;
  mod = fmod(a, b);
  if (mod) {
    if ((b < 0) != (mod < 0))
      mod += b;
  } else {
    mod = copysign(0.0, b);
  }
  *res = mod;
  return 1;
}


/* Python's float floor division, the floordiv half of float_divmod:
   the quotient of the divisor-signed remainder, floored, nudged up
   where the division rounded it down past a half, and a zero result
   carrying the quotient's sign. */
KOS_OP_INLINE int kos_d_floor_divide(double a, double b, double *res) {
  double mod, div, floordiv;

  if (unlikely(b == 0.0))
    return 0;
  mod = fmod(a, b);
  div = (a - mod) / b;
  if (mod) {
    if ((b < 0) != (mod < 0)) {
      mod += b;
      div -= 1.0;
    }
  } else {
    mod = copysign(0.0, b);
  }
  if (div) {
    floordiv = floor(div);
    if (div - floordiv > 0.5)
      floordiv += 1.0;
  } else {
    floordiv = copysign(0.0, a / b);
  }
  *res = floordiv;
  return 1;
}


/* The _d forms: one step, materialised into the first of the two
   reuse candidates that can take it, else a fresh float. */
#define KOS_FLOAT_FAST_D_FORM(NAME)                                     \
  KOS_OP_INLINE int float_fast_##NAME##_d(                              \
      double a, double b, PyObject *reuse, PyObject *reuse2,            \
      PyObject **out) {                                                 \
    double res;                                                         \
    if (unlikely(! kos_d_##NAME(a, b, &res)))                           \
      return 0;                                                         \
    return kos_float_fast_result(res, reuse, reuse2, out);              \
  }

KOS_FLOAT_FAST_D_FORM(add)
KOS_FLOAT_FAST_D_FORM(subtract)
KOS_FLOAT_FAST_D_FORM(multiply)
KOS_FLOAT_FAST_D_FORM(true_divide)
KOS_FLOAT_FAST_D_FORM(modulo)
KOS_FLOAT_FAST_D_FORM(floor_divide)

#undef KOS_FLOAT_FAST_D_FORM


/* The object forms: both exact floats, then the _d form; and the num
   forms: the int path, then the float path. */
#define KOS_FLOAT_FAST_OBJECT_FORM(NAME)                                \
  KOS_OP_INLINE int float_fast_##NAME(                                  \
      PyObject *l, PyObject *r, PyObject *reuse, PyObject **out) {      \
    if (! (PyFloat_CheckExact(l) && PyFloat_CheckExact(r)))             \
      return 0;                                                         \
    return float_fast_##NAME##_d(                                       \
        PyFloat_AS_DOUBLE(l), PyFloat_AS_DOUBLE(r), reuse, NULL, out);  \
  }                                                                     \
  KOS_OP_INLINE int num_fast_##NAME(                                    \
      PyObject *l, PyObject *r, PyObject *reuse, PyObject **out) {      \
    int rc = long_fast_##NAME(l, r, reuse, out);                        \
    if (rc)                                                             \
      return rc;                                                        \
    return float_fast_##NAME(l, r, reuse, out);                         \
  }

KOS_FLOAT_FAST_OBJECT_FORM(add)
KOS_FLOAT_FAST_OBJECT_FORM(subtract)
KOS_FLOAT_FAST_OBJECT_FORM(multiply)
KOS_FLOAT_FAST_OBJECT_FORM(true_divide)
KOS_FLOAT_FAST_OBJECT_FORM(modulo)
KOS_FLOAT_FAST_OBJECT_FORM(floor_divide)

#undef KOS_FLOAT_FAST_OBJECT_FORM


/* === PyUnicode fast paths === */

/* Concatenation of exact strs, shaped like the numeric paths: a pair
   form for the operator cores, an append form for a caller that owns
   the left outright, and a join over any count for the reducing forms
   of + and for build_str. A str subclass takes the generic path, so
   its __add__ and __radd__ stay in charge. */

/* str + str for two borrowed operands. Returns 1 with *out set, 0
   when the pair is not two exact strs, -1 with an error set. */
KOS_OP_INLINE int str_fast_add(PyObject *l, PyObject *r, PyObject **out) {
  if (! (PyUnicode_CheckExact(l) && PyUnicode_CheckExact(r)))
    return 0;
  *out = PyUnicode_Concat(l, r);
  return likely(*out != NULL) ? 1 : -1;
}


/* left += right for an OWNED exact str left and a borrowed exact str
   right: the reference to left is consumed and *out is the result
   reference. PyUnicode_Append grows the left in place under CPython's
   own rule (a sole reference, no cached hash, not interned, the
   right's kind no wider), so a loop growing one accumulator gets the
   amortised resize that `s += x` gets in CPython, and concatenates
   otherwise. The left may move, so the caller stores *out back to
   wherever it lived. Returns 1, or -1 with an error set and the left
   released. */
KOS_OP_INLINE int str_fast_append(
    PyObject *left, PyObject *right, PyObject **out) {

  *out = left;
  PyUnicode_Append(out, right);
  return likely(*out != NULL) ? 1 : -1;
}


/* The join of count borrowed operands: one allocation of the total
   length at the widest kind, then each piece copied in; a single
   non-empty piece is returned itself, as str.join does. Returns 1
   with *out set, 0 when an operand is not an exact str (nothing
   allocated), -1 with an error set. */
static inline int str_fast_join(
    PyObject *const *args, size_t count, PyObject **out) {

  Py_ssize_t total = 0;
  Py_UCS4 maxchar = 0;
  PyObject *sole = NULL;
  size_t pieces = 0;
  PyObject *res;
  Py_ssize_t pos = 0;

  for (size_t i = 0; i < count; i++) {
    PyObject *s = args[i];
    Py_ssize_t n;
    Py_UCS4 mc;

    if (! PyUnicode_CheckExact(s))
      return 0;
#if PY_VERSION_HEX < 0x030c0000
    if (unlikely(PyUnicode_READY(s) < 0))
      return -1;
#endif
    n = PyUnicode_GET_LENGTH(s);
    if (! n)
      continue;
    if (unlikely(n > PY_SSIZE_T_MAX - total)) {
      PyErr_SetString(PyExc_OverflowError, "join() result is too long");
      return -1;
    }
    total += n;
    mc = PyUnicode_MAX_CHAR_VALUE(s);
    if (mc > maxchar)
      maxchar = mc;
    sole = s;
    pieces++;
  }

  if (pieces <= 1) {
    if (sole) {
      Py_INCREF(sole);
      res = sole;
    } else {
      res = PyUnicode_New(0, 0);
    }
    *out = res;
    return likely(res != NULL) ? 1 : -1;
  }

  res = PyUnicode_New(total, maxchar);
  if (unlikely(! res))
    return -1;

  for (size_t i = 0; i < count; i++) {
    PyObject *s = args[i];
    Py_ssize_t n = PyUnicode_GET_LENGTH(s);

    if (! n)
      continue;
    if (unlikely(PyUnicode_CopyCharacters(res, pos, s, 0, n) < 0)) {
      Py_DECREF(res);
      return -1;
    }
    pos += n;
  }

  *out = res;
  return 1;
}


/* A richcompare selector over two doubles, with the C operators so a
   NaN answers as Python does: false to everything but not-equal. */
KOS_OP_INLINE int kos_double_compare(KosCompare sel, double a, double b) {
  switch (sel) {
    case KOS_COMPARE_LT: return a < b;
    case KOS_COMPARE_LE: return a <= b;
    case KOS_COMPARE_EQ: return a == b;
    case KOS_COMPARE_NE: return a != b;
    case KOS_COMPARE_GT: return a > b;
    default: return a >= b;
  }
}


/**
   Reducing or otherwise special-case operators

   These operators accept a variable number of arguments and reduce
   them using the corresponding Python number protocol function. For
   example, KosOperator_Add reduces multiple values using
   PyNumber_Add.
*/


typedef PyObject *KosOperatorReducing(PyObject *const *values, size_t count);


PyObject *KosOperator_Add(PyObject *const *values, size_t count);

PyObject *KosOperator_BitwiseAnd(PyObject *const *values, size_t count);

PyObject *KosOperator_BitwiseOr(PyObject *const *values, size_t count);

PyObject *KosOperator_BitwiseXor(PyObject *const *values, size_t count);

PyObject *KosOperator_FloorDivide(PyObject *const *values, size_t count);

PyObject *KosOperator_MatrixMultiply(PyObject *const *values, size_t count);

/* Not a reduce: two values are base and exponent, three add the
   modulus of three-argument pow. Any other count is a TypeError. */
PyObject *KosOperator_Power(PyObject *const *values, size_t count);

PyObject *KosOperator_Multiply(PyObject *const *values, size_t count);

PyObject *KosOperator_Subtract(PyObject *const *values, size_t count);

PyObject *KosOperator_TrueDivide(PyObject *const *values, size_t count);


/**
   Operator dispatch

   Select a reducing / in-place-reducing core by operator selector and apply it
   to values[0..count-1] (values[0] is the accumulator seed). Used by the
   OBJECT_OPERAND / *_INPLACE opcode handlers. The whole arithmetic surface --
   every per-operator core, the two-operand PyLong fast paths, and both of these
   switches -- lives in operator.c; the reducing (%, <<, >>, **) and all
   in-place cores have no runtime export and are reached only through these.
*/


PyObject *kos_operand_reduce(
    KosOperand op, PyObject *const *values, size_t count);

PyObject *kos_inplace_reduce(
    KosOperand op, PyObject *const *values, size_t count);


/**
   Comparison dispatch

   Select a comparison core by KosCompare selector. The richcompare six
   pass the selector straight through to PyObject_RichCompare(Bool) and
   chain-reduce over count operands; the extended selectors preserve
   their prior per-opcode cores (identity, contains, isinstance,
   issubclass) verbatim. Used by the CHECK_COMPARE / OBJECT_COMPARE
   opcode handlers.
*/


/* Cold extended tail of kos_compare_chain: everything past the
   richcompare six. Reached only through the inline fast path below. */
int kos_compare_chain_ext(
    KosCompare sel, PyObject *const *args, size_t count);

/* Compare two extracted machine ints under a richcompare-six selector:
   trichotomy bit-test, no object traffic.  Each mask holds the
   selector's outcome for c = (a > b) - (a < b) at bit c + 1.  Only
   valid after kos_as_ll2 accepts and for sel <= KOS_COMPARE_GE. */
static inline int kos_ll_compare(
    KosCompare sel,
    long long a,
    long long b) {

  static const unsigned char masks[6] = { 1, 3, 2, 5, 4, 6 };
  int c = (a > b) - (a < b);
  return (masks[sel] >> (c + 1)) & 1;
}

/* chain-evaluate; 1 = held, 0 = failed (branch), -1 = error. The
   richcompare six stay inline at the call site -- the selector IS the
   Py_LT..Py_GE ordinal, passed through untranslated -- so the dispatch
   loop's check path keeps the pre-unification inlining.  Exact-int
   pairs settle on the trichotomy bit-test without touching the
   richcompare machinery.  Every other pair takes the full richcompare
   and the truth of its result, not PyObject_RichCompareBool, whose
   identity shortcut answers EQ and NE by pointer before the type is
   asked; a branch must ask, as the value form and Python do, so a NaN
   is unequal to itself and a type may answer for its own identity. */
static inline int kos_compare_chain(
    KosCompare sel,
    PyObject *const *args,
    size_t count) {

  if (likely(sel <= KOS_COMPARE_GE)) {
    int res = 1;
    for (size_t i = 1; i < count; i++) {
      PyObject *l = args[i - 1];
      PyObject *r = args[i];
      long long a, b;
      if (likely(kos_as_ll2(l, r, &a, &b))) {
        res = kos_ll_compare(sel, a, b);
      } else {
        PyObject *truth = PyObject_RichCompare(l, r, (int) sel);
        if (! truth)
          return -1;
        if (truth == Py_True)
          res = 1;
        else if (truth == Py_False)
          res = 0;
        else
          res = PyObject_IsTrue(truth);
        Py_DECREF(truth);
      }

      if (res <= 0)
        break;
    }
    return res;
  }

  return kos_compare_chain_ext(sel, args, count);
}

/* same reduction, returning a new reference (last richcompare result
   for the six; bool for the extended selectors) */
PyObject *kos_compare_reduce(
    KosCompare sel, PyObject *const *args, size_t count);


PyObject *KosOperator_In(PyObject *const *values, size_t count);

PyObject *KosOperator_NotIn(PyObject *const *value, size_t count);

PyObject *KosOperator_BuildStr(PyObject *const *value, size_t count);


PyObject *KosOperator_Eq(PyObject *const *values, size_t count);

PyObject *KosOperator_Ne(PyObject *const *values, size_t count);

PyObject *KosOperator_Lt(PyObject *const *values, size_t count);

PyObject *KosOperator_Le(PyObject *const *values, size_t count);

PyObject *KosOperator_Gt(PyObject *const *values, size_t count);

PyObject *KosOperator_Ge(PyObject *const *values, size_t count);


/* === unit lifecycle === */


int koskari_operator_init(PyObject *mod);
int koskari_operator_clear(PyObject *mod);


#endif


// The end.
