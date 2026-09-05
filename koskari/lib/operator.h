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


/* Read an exact int of at most two digits straight from its digits.
   Two 30-bit digits are at most 60 bits, so the value always fits and
   there is no overflow test. Returns 1 with *out set, or 0 for a wider
   int, which the caller converts the general way. */
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
    default:
      return 0;
  }

  *out = KOS_LONG_NEGATIVE(lo) ? -mag : mag;
  return 1;
#else
  (void) v;
  (void) out;
  return 0;
#endif
}


/* An exact int as a C long long: the two-digit read first, then the
   general conversion for anything wider. Returns 1 with *out set, or 0
   when the value does not fit. */
KOS_OP_INLINE int kos_long_as_ll(PyObject *v, long long *out) {
  if (likely(kos_long_as_ll_digits(v, out)))
    return 1;

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
   and then the _ll form with no candidate, for the operator cores.

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


/* Materialise res: into reuse when the caller offered an operand it
   owns outright and kos_long_reusable allows it, in which case *out is
   that same object and the caller's reference to it becomes the result
   reference; else a fresh int. Returns 1, or -1 with an error set. */
KOS_OP_INLINE int kos_long_fast_result(
    long long res, PyObject *reuse, PyObject **out) {

#if KOS_LONG_REUSE
  if (reuse && kos_long_reusable(reuse, res)) {
    kos_long_write(reuse, res);
    *out = reuse;
    return 1;
  }
#else
  (void) reuse;
#endif
  *out = PyLong_FromLongLong(res);
  return likely(*out != NULL) ? 1 : -1;
}


KOS_OP_INLINE int long_fast_add_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  long long res;
  if (unlikely(__builtin_add_overflow(a, b, &res)))
    return 0;
  return kos_long_fast_result(res, reuse, out);
}


KOS_OP_INLINE int long_fast_subtract_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  long long res;
  if (unlikely(__builtin_sub_overflow(a, b, &res)))
    return 0;
  return kos_long_fast_result(res, reuse, out);
}


KOS_OP_INLINE int long_fast_multiply_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  long long res;
  if (unlikely(__builtin_mul_overflow(a, b, &res)))
    return 0;
  return kos_long_fast_result(res, reuse, out);
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


KOS_OP_INLINE int long_fast_modulo_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  long long q, r;
  if (unlikely(! kos_ll_floor_divmod(a, b, &q, &r)))
    return 0;
  return kos_long_fast_result(r, reuse, out);
}


KOS_OP_INLINE int long_fast_floor_divide_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  long long q, r;
  if (unlikely(! kos_ll_floor_divmod(a, b, &q, &r)))
    return 0;
  return kos_long_fast_result(q, reuse, out);
}


/* Two ints within 53 bits divide exactly as doubles: IEEE division of
   exactly represented values is correctly rounded, which is what
   int.__truediv__ computes for them. Wider operands and a zero divisor
   decline. */
KOS_OP_INLINE int long_fast_true_divide_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  const long long lim = 1LL << 53;
  (void) reuse;
  if (unlikely(b == 0 || a <= -lim || a >= lim || b <= -lim || b >= lim))
    return 0;
  *out = PyFloat_FromDouble((double) a / (double) b);
  return likely(*out != NULL) ? 1 : -1;
}


/* Non-negative exponents by squaring, declining on overflow; a
   negative exponent is a float result and declines. */
KOS_OP_INLINE int long_fast_power_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  long long res = 1;
  long long base = a;

  if (unlikely(b < 0))
    return 0;
  while (b) {
    if ((b & 1) && unlikely(__builtin_mul_overflow(res, base, &res)))
      return 0;
    b >>= 1;
    if (b && unlikely(__builtin_mul_overflow(base, base, &base)))
      return 0;
  }
  return kos_long_fast_result(res, reuse, out);
}


KOS_OP_INLINE int long_fast_bit_and_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  return kos_long_fast_result(a & b, reuse, out);
}


KOS_OP_INLINE int long_fast_bit_or_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  return kos_long_fast_result(a | b, reuse, out);
}


KOS_OP_INLINE int long_fast_bit_xor_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  return kos_long_fast_result(a ^ b, reuse, out);
}


/* A negative left operand shifts through its magnitude, so the C shift
   never sees a negative value; LLONG_MIN's magnitude does not fit and
   declines. */
KOS_OP_INLINE int long_fast_bit_left_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  long long mag;

  if (unlikely(b < 0 || b >= 63))
    return 0;
  if (a >= 0) {
    if (unlikely(a > (LLONG_MAX >> b)))
      return 0;
    return kos_long_fast_result(a << b, reuse, out);
  }
  if (unlikely(a == LLONG_MIN))
    return 0;
  mag = -a;
  if (unlikely(mag > (LLONG_MAX >> b)))
    return 0;
  return kos_long_fast_result(-(mag << b), reuse, out);
}


/* A negative left operand takes the floor shift Python defines, spelled
   through the complement so no negative value reaches the C shift. */
KOS_OP_INLINE int long_fast_bit_right_ll(long long a, long long b, PyObject *reuse, PyObject **out) {
  if (unlikely(b < 0))
    return 0;
  if (b >= 63)
    return kos_long_fast_result(a < 0 ? -1 : 0, reuse, out);
  return kos_long_fast_result(a >= 0 ? (a >> b) : ~((~a) >> b), reuse, out);
}


/* The object forms: extract, then the _ll form. */
#define KOS_LONG_FAST_OBJECT_FORM(NAME)                                 \
  KOS_OP_INLINE int long_fast_##NAME(                                   \
      PyObject *l, PyObject *r, PyObject **out) {                       \
    long long a, b;                                                     \
    if (! kos_as_ll2(l, r, &a, &b))                                     \
      return 0;                                                         \
    return long_fast_##NAME##_ll(a, b, NULL, out);                            \
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
   richcompare machinery; identity implies value equality for ints, so
   the shortcut PyObject_RichCompareBool takes on EQ/NE is preserved. */
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
      if (likely(kos_as_ll2(l, r, &a, &b)))
        res = kos_ll_compare(sel, a, b);
      else
        res = PyObject_RichCompareBool(l, r, (int) sel);
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
