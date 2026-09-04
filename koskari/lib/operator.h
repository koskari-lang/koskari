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
#  define KOS_LONG_NDIGITS(lo)  ((size_t) ((lo)->long_value.lv_tag >> 3))
#  define KOS_LONG_NEGATIVE(lo) (((lo)->long_value.lv_tag & 3) == 2)
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


/* Read an exact int of at most two digits straight from its digits.
   Two 30-bit digits are at most 60 bits, so the value always fits and
   there is no overflow test. Returns 1 with *out set, or 0 for a wider
   int, which the caller converts the general way. */
static inline int kos_long_as_ll_digits(PyObject *v, long long *out) {
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
static inline int kos_long_as_ll(PyObject *v, long long *out) {
  if (likely(kos_long_as_ll_digits(v, out)))
    return 1;

  int overflow;
  *out = PyLong_AsLongLongAndOverflow(v, &overflow);
  return ! overflow;
}


/* Both operands exact ints that fit in a C long long.  Mechanical extraction
   only, with no operator knowledge, so it never re-branches on op.  Returns 1
   with both *a and *b set, or 0 when either operand declines the fast path. */
static inline int kos_as_ll2(
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
