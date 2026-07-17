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


#include "lib.h"
#include "hints.h"

#include <limits.h>


static PyObject *_str__empty = NULL;


/* === PyLong two-operand fast paths === */


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

  int overflow;
  *a = PyLong_AsLongLongAndOverflow(l, &overflow);
  if (unlikely(overflow))
    return 0;
  *b = PyLong_AsLongLongAndOverflow(r, &overflow);
  if (unlikely(overflow))
    return 0;

  return 1;
}


/* Per-operator two-operand PyLong fast paths.  Each is self-contained and
   written out explicitly -- no shared operator switch -- so a caller that
   already knows its operator lands straight in the one arithmetic it needs.
   When both operands are exact ints and the operation cannot lose precision,
   compute it directly and skip the PyNumber slot dispatch and longobject digit
   loops.

   Return convention (all of them): 1 with *out set (new ref) when handled, 0
   when declined (*out untouched), -1 with an error set on allocation failure
   (*out NULL).  Bitwise AND/OR/XOR on two's-complement match Python int
   semantics for any in-range operands.  Shifts require non-negative operands
   (C >> on a negative is implementation-defined; a negative count must raise).
   Modulo requires a non-negative dividend and positive divisor so C % matches
   Python remainder.  Overflow on +, -, *, << declines rather than widening.
   Divide, floor-divide, matmul, and power have no fast path. */


typedef int (*KosLongFast)(PyObject *l, PyObject *r, PyObject **out);


static inline int long_fast_add(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b, res;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  if (unlikely(__builtin_add_overflow(a, b, &res)))
    return 0;
  *out = PyLong_FromLongLong(res);
  return likely(*out != NULL) ? 1 : -1;
}


static inline int long_fast_subtract(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b, res;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  if (unlikely(__builtin_sub_overflow(a, b, &res)))
    return 0;
  *out = PyLong_FromLongLong(res);
  return likely(*out != NULL) ? 1 : -1;
}


static inline int long_fast_multiply(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b, res;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  if (unlikely(__builtin_mul_overflow(a, b, &res)))
    return 0;
  *out = PyLong_FromLongLong(res);
  return likely(*out != NULL) ? 1 : -1;
}


static inline int long_fast_modulo(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  if (unlikely(a < 0 || b <= 0))
    return 0;
  *out = PyLong_FromLongLong(a % b);
  return likely(*out != NULL) ? 1 : -1;
}


static inline int long_fast_bit_and(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  *out = PyLong_FromLongLong(a & b);
  return likely(*out != NULL) ? 1 : -1;
}


static inline int long_fast_bit_or(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  *out = PyLong_FromLongLong(a | b);
  return likely(*out != NULL) ? 1 : -1;
}


static inline int long_fast_bit_xor(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  *out = PyLong_FromLongLong(a ^ b);
  return likely(*out != NULL) ? 1 : -1;
}


static inline int long_fast_bit_left(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  if (unlikely(a < 0 || b < 0 || b >= 63))
    return 0;
  if (unlikely(a != 0 && a > (LLONG_MAX >> b)))
    return 0;
  *out = PyLong_FromLongLong(a << b);
  return likely(*out != NULL) ? 1 : -1;
}


static inline int long_fast_bit_right(PyObject *l, PyObject *r, PyObject **out) {
  long long a, b;
  if (! kos_as_ll2(l, r, &a, &b))
    return 0;
  if (unlikely(a < 0 || b < 0))
    return 0;
  *out = PyLong_FromLongLong((b >= 63) ? 0 : (a >> b));
  return likely(*out != NULL) ? 1 : -1;
}


#define PICK_FUNC(name) (PyCFunction) m_ ## name


#if PY_MINOR_VERSION < 6
#define METHOD_TYPE METH_VARARGS
#define VAR_FUNC(name)                                                  \
  static PyObject *m_ ## name(PyObject *mod,                            \
                              PyObject *args) {                         \
    return name(PySequence_Fast_ITEMS(args),                            \
                (size_t) PySequence_Fast_GET_SIZE(args));               \
  }

#else
#define METHOD_TYPE METH_FASTCALL
#define VAR_FUNC(name)                                                  \
  static PyObject *m_ ## name (PyObject *mod,                           \
                               PyObject *const *args,                   \
                               Py_ssize_t count) {                      \
    return name(args, count);                                           \
  }

#endif


static inline PyObject *chain_compare(
    int opid,
    const char *name,
    PyObject *const *args,
    size_t count) {

  if (count < 2) {
    PyErr_Format(PyExc_TypeError, "%s requires at least 2"
                 " positional arguments", name);
    return NULL;
  }

  PyObject *value = PyObject_RichCompare(args[0], args[1], opid);
  if (unlikely(! value) || PyObject_Not(value))
    return value;

  for (size_t i = 2; i < (size_t) count; i++) {
    Py_DECREF(value);
    value = PyObject_RichCompare(args[i - 1], args[i], opid);
    if (unlikely(! value) || PyObject_Not(value))
      return value;
  }

  return value;
}


PyObject *KosOperator_Eq(PyObject *const *values, size_t count) {
  return chain_compare(Py_EQ, "==", values, count);
}


PyObject *KosOperator_Ne(PyObject *const *values, size_t count) {
  return chain_compare(Py_NE, "!=", values, count);
}


PyObject *KosOperator_Lt(PyObject *const *values, size_t count) {
  return chain_compare(Py_LT, "<", values, count);
}


PyObject *KosOperator_Le(PyObject *const *values, size_t count) {
  return chain_compare(Py_LE, "<=", values, count);
}


PyObject *KosOperator_Gt(PyObject *const *values, size_t count) {
  return chain_compare(Py_GT, ">", values, count);
}


PyObject *KosOperator_Ge(PyObject *const *values, size_t count) {
  return chain_compare(Py_GE, ">=", values, count);
}


/* === Reducing operator cores === */


/* Cold reduce tail: the arity edges (0, 1) and the true reduce (count > 2).
   The count == 2 hot case never reaches here -- each core handles it inline --
   so this stays out of line, keeping the inlined cores tiny.  longfast is the
   operator's PyLong fast path (NULL for operators without one); func / solofunc
   are its binary and unary protocol slots. */
static KOS_NOINLINE PyObject *reduce_slow(
    const char *name,
    KosLongFast longfast,
    binaryfunc func,
    unaryfunc solofunc,
    PyObject *const *args,
    size_t count) {

  if (count == 1) {
    if (solofunc)
      return solofunc(args[0]);
    PyErr_Format(PyExc_TypeError,
                 "%s requires at least 2 positional arguments", name);
    return NULL;
  }

  if (count == 0) {
    PyErr_Format(PyExc_TypeError, "%s requires at least %s positional %s",
                 name, solofunc ? "1" : "2",
                 solofunc ? "argument" : "arguments");
    return NULL;
  }

  // count > 2: fold left-to-right, value is the owned accumulator
  PyObject *value = args[0];
  Py_INCREF(value);

  for (size_t i = 1; i < count; i++) {
    PyObject *n = NULL;
    if (! longfast || longfast(value, args[i], &n) == 0)
      n = func(value, args[i]);
    Py_DECREF(value);
    value = n;
    if (! value)
      break;
  }

  return value;
}


/* Self-contained per-operator cores.  Each owns its whole shape: the two-
   operand hot path (its own PyLong fast path, then its concrete PyNumber slot,
   no operator switch and no function-pointer indirection) and, for every other
   arity, a tail into the shared cold reduce_slow.  Exposed two ways -- as the
   runtime KosOperator_* form and as a switch case in the dispatchers below --
   both of which inline these bodies.  The in-place cores mirror the reducing
   ones through the PyNumber_InPlace* slots; the PyLong fast path is shared,
   since int OP= int is value-identical to int OP int. */


KOS_OP_INLINE PyObject *op_add(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_add(args[0], args[1], &out) == 0)
      out = PyNumber_Add(args[0], args[1]);
    return out;
  }
  return reduce_slow("+", long_fast_add, PyNumber_Add, PyNumber_Positive,
                     args, count);
}


KOS_OP_INLINE PyObject *op_subtract(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_subtract(args[0], args[1], &out) == 0)
      out = PyNumber_Subtract(args[0], args[1]);
    return out;
  }
  return reduce_slow("-", long_fast_subtract, PyNumber_Subtract,
                     PyNumber_Negative, args, count);
}


KOS_OP_INLINE PyObject *op_multiply(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_multiply(args[0], args[1], &out) == 0)
      out = PyNumber_Multiply(args[0], args[1]);
    return out;
  }
  return reduce_slow("*", long_fast_multiply, PyNumber_Multiply, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_modulo(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_modulo(args[0], args[1], &out) == 0)
      out = PyNumber_Remainder(args[0], args[1]);
    return out;
  }
  return reduce_slow("%", long_fast_modulo, PyNumber_Remainder, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_bit_and(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_and(args[0], args[1], &out) == 0)
      out = PyNumber_And(args[0], args[1]);
    return out;
  }
  return reduce_slow("&", long_fast_bit_and, PyNumber_And, NULL, args, count);
}


KOS_OP_INLINE PyObject *op_bit_or(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_or(args[0], args[1], &out) == 0)
      out = PyNumber_Or(args[0], args[1]);
    return out;
  }
  return reduce_slow("|", long_fast_bit_or, PyNumber_Or, NULL, args, count);
}


KOS_OP_INLINE PyObject *op_bit_xor(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_xor(args[0], args[1], &out) == 0)
      out = PyNumber_Xor(args[0], args[1]);
    return out;
  }
  return reduce_slow("^", long_fast_bit_xor, PyNumber_Xor, NULL, args, count);
}


KOS_OP_INLINE PyObject *op_bit_left(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_left(args[0], args[1], &out) == 0)
      out = PyNumber_Lshift(args[0], args[1]);
    return out;
  }
  return reduce_slow("<<", long_fast_bit_left, PyNumber_Lshift, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_bit_right(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_right(args[0], args[1], &out) == 0)
      out = PyNumber_Rshift(args[0], args[1]);
    return out;
  }
  return reduce_slow(">>", long_fast_bit_right, PyNumber_Rshift, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_true_divide(PyObject *const *args, size_t count) {
  if (likely(count == 2))
    return PyNumber_TrueDivide(args[0], args[1]);
  return reduce_slow("/", NULL, PyNumber_TrueDivide, NULL, args, count);
}


KOS_OP_INLINE PyObject *op_floor_divide(PyObject *const *args, size_t count) {
  if (likely(count == 2))
    return PyNumber_FloorDivide(args[0], args[1]);
  return reduce_slow("//", NULL, PyNumber_FloorDivide, NULL, args, count);
}


KOS_OP_INLINE PyObject *op_matrix_multiply(
    PyObject *const *args, size_t count) {
  if (likely(count == 2))
    return PyNumber_MatrixMultiply(args[0], args[1]);
  return reduce_slow("@", NULL, PyNumber_MatrixMultiply, NULL, args, count);
}


/* Power is 2-arg (base, exp) or 3-arg (base, exp, mod); not a reduce and no
   PyLong fast path. */
KOS_OP_INLINE PyObject *op_power(PyObject *const *args, size_t count) {
  if (count == 2)
    return PyNumber_Power(args[0], args[1], Py_None);
  if (count == 3)
    return PyNumber_Power(args[0], args[1], args[2]);
  PyErr_SetString(PyExc_TypeError,
                  "** requires 2 or 3 positional arguments");
  return NULL;
}


KOS_OP_INLINE PyObject *op_inplace_add(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_add(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceAdd(args[0], args[1]);
    return out;
  }
  return reduce_slow("+=", long_fast_add, PyNumber_InPlaceAdd, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_subtract(
    PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_subtract(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceSubtract(args[0], args[1]);
    return out;
  }
  return reduce_slow("-=", long_fast_subtract, PyNumber_InPlaceSubtract, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_multiply(
    PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_multiply(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceMultiply(args[0], args[1]);
    return out;
  }
  return reduce_slow("*=", long_fast_multiply, PyNumber_InPlaceMultiply, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_modulo(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_modulo(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceRemainder(args[0], args[1]);
    return out;
  }
  return reduce_slow("%=", long_fast_modulo, PyNumber_InPlaceRemainder, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_bit_and(
    PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_and(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceAnd(args[0], args[1]);
    return out;
  }
  return reduce_slow("&=", long_fast_bit_and, PyNumber_InPlaceAnd, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_bit_or(PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_or(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceOr(args[0], args[1]);
    return out;
  }
  return reduce_slow("|=", long_fast_bit_or, PyNumber_InPlaceOr, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_bit_xor(
    PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_xor(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceXor(args[0], args[1]);
    return out;
  }
  return reduce_slow("^=", long_fast_bit_xor, PyNumber_InPlaceXor, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_bit_left(
    PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_left(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceLshift(args[0], args[1]);
    return out;
  }
  return reduce_slow("<<=", long_fast_bit_left, PyNumber_InPlaceLshift, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_bit_right(
    PyObject *const *args, size_t count) {
  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (long_fast_bit_right(args[0], args[1], &out) == 0)
      out = PyNumber_InPlaceRshift(args[0], args[1]);
    return out;
  }
  return reduce_slow(">>=", long_fast_bit_right, PyNumber_InPlaceRshift, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_true_divide(
    PyObject *const *args, size_t count) {
  if (likely(count == 2))
    return PyNumber_InPlaceTrueDivide(args[0], args[1]);
  return reduce_slow("/=", NULL, PyNumber_InPlaceTrueDivide, NULL, args, count);
}


KOS_OP_INLINE PyObject *op_inplace_floor_divide(
    PyObject *const *args, size_t count) {
  if (likely(count == 2))
    return PyNumber_InPlaceFloorDivide(args[0], args[1]);
  return reduce_slow("//=", NULL, PyNumber_InPlaceFloorDivide, NULL,
                     args, count);
}


KOS_OP_INLINE PyObject *op_inplace_matrix_multiply(
    PyObject *const *args, size_t count) {
  if (likely(count == 2))
    return PyNumber_InPlaceMatrixMultiply(args[0], args[1]);
  return reduce_slow("@=", NULL, PyNumber_InPlaceMatrixMultiply, NULL,
                     args, count);
}


/* Augmented power is strictly binary (base, exp); the ternary modulus form has
   no augmented spelling. */
KOS_OP_INLINE PyObject *op_inplace_power(PyObject *const *args, size_t count) {
  if (count != 2) {
    PyErr_SetString(PyExc_TypeError,
                    "**= requires exactly 2 positional arguments");
    return NULL;
  }
  return PyNumber_InPlacePower(args[0], args[1], Py_None);
}


/* === Runtime operator exports === */


/* Thin wrappers exposing the reducing cores as operator-module functions (the
   first-class `+`, `-`, ... values).  The in-place cores and the dispatch-only
   reducing cores (%, <<, >>, **) have no runtime export -- they are reached
   only through the dispatchers below. */


PyObject *KosOperator_Add(PyObject *const *values, size_t count) {
  return op_add(values, count);
}


PyObject *KosOperator_Subtract(PyObject *const *values, size_t count) {
  return op_subtract(values, count);
}


PyObject *KosOperator_Multiply(PyObject *const *values, size_t count) {
  return op_multiply(values, count);
}


PyObject *KosOperator_BitwiseAnd(PyObject *const *values, size_t count) {
  return op_bit_and(values, count);
}


PyObject *KosOperator_BitwiseOr(PyObject *const *values, size_t count) {
  return op_bit_or(values, count);
}


PyObject *KosOperator_BitwiseXor(PyObject *const *values, size_t count) {
  return op_bit_xor(values, count);
}


PyObject *KosOperator_FloorDivide(PyObject *const *values, size_t count) {
  return op_floor_divide(values, count);
}


PyObject *KosOperator_TrueDivide(PyObject *const *values, size_t count) {
  return op_true_divide(values, count);
}


PyObject *KosOperator_MatrixMultiply(PyObject *const *values, size_t count) {
  return op_matrix_multiply(values, count);
}


/* === Operator dispatch === */


/* Select the reducing core by operator selector and apply it to
   values[0..count-1] (values[0] is the accumulator seed).  One switch, no
   further branching: each case inlines its self-contained core.  Returns a new
   reference or NULL. */
PyObject *kos_operand_reduce(
    KosOperand op,
    PyObject *const *values,
    size_t count) {

  switch (op) {
    case KOS_OPERAND_ADD:
      return op_add(values, count);
    case KOS_OPERAND_SUBTRACT:
      return op_subtract(values, count);
    case KOS_OPERAND_MULTIPLY:
      return op_multiply(values, count);
    case KOS_OPERAND_MODULO:
      return op_modulo(values, count);
    case KOS_OPERAND_BIT_AND:
      return op_bit_and(values, count);
    case KOS_OPERAND_BIT_OR:
      return op_bit_or(values, count);
    case KOS_OPERAND_BIT_XOR:
      return op_bit_xor(values, count);
    case KOS_OPERAND_BIT_LEFT:
      return op_bit_left(values, count);
    case KOS_OPERAND_BIT_RIGHT:
      return op_bit_right(values, count);
    case KOS_OPERAND_TRUE_DIVIDE:
      return op_true_divide(values, count);
    case KOS_OPERAND_FLOOR_DIVIDE:
      return op_floor_divide(values, count);
    case KOS_OPERAND_MATRIX_MULTIPLY:
      return op_matrix_multiply(values, count);
    case KOS_OPERAND_POWER:
      return op_power(values, count);
    default:
      PyErr_Format(PyExc_SystemError, "bad operand op-type: %i", (int) op);
      return NULL;
  }
}


/* In-place counterpart of kos_operand_reduce: the same selector over the
   in-place cores (values[0] is the accumulator seed).  Returns a new reference
   or NULL. */
PyObject *kos_inplace_reduce(
    KosOperand op,
    PyObject *const *values,
    size_t count) {

  switch (op) {
    case KOS_OPERAND_ADD:
      return op_inplace_add(values, count);
    case KOS_OPERAND_SUBTRACT:
      return op_inplace_subtract(values, count);
    case KOS_OPERAND_MULTIPLY:
      return op_inplace_multiply(values, count);
    case KOS_OPERAND_MODULO:
      return op_inplace_modulo(values, count);
    case KOS_OPERAND_BIT_AND:
      return op_inplace_bit_and(values, count);
    case KOS_OPERAND_BIT_OR:
      return op_inplace_bit_or(values, count);
    case KOS_OPERAND_BIT_XOR:
      return op_inplace_bit_xor(values, count);
    case KOS_OPERAND_BIT_LEFT:
      return op_inplace_bit_left(values, count);
    case KOS_OPERAND_BIT_RIGHT:
      return op_inplace_bit_right(values, count);
    case KOS_OPERAND_TRUE_DIVIDE:
      return op_inplace_true_divide(values, count);
    case KOS_OPERAND_FLOOR_DIVIDE:
      return op_inplace_floor_divide(values, count);
    case KOS_OPERAND_MATRIX_MULTIPLY:
      return op_inplace_matrix_multiply(values, count);
    case KOS_OPERAND_POWER:
      return op_inplace_power(values, count);
    default:
      PyErr_Format(PyExc_SystemError,
                   "bad inplace operand op-type: %i", (int) op);
      return NULL;
  }
}


/* === Comparison dispatch === */


/* Display names for the richcompare selectors, indexed by KosCompare
   ordinal (== Py_LT..Py_GE). */
static const char *compare_names[] = {
  "<", "<=", "==", "!=", ">", ">=",
};


/* Arity-2 guard shared by the extended comparison selectors. */
static int compare_require_2(KosCompare sel, size_t count) {
  if (count != 2) {
    PyErr_Format(PyExc_TypeError, "comparison selector %i requires 2"
                 " positional arguments", (int) sel);
    return -1;
  }
  return 0;
}


/* Cold extended tail of kos_compare_chain (see operator.h): everything
   past the richcompare six. */
KOS_NOINLINE int kos_compare_chain_ext(
    KosCompare sel,
    PyObject *const *args,
    size_t count) {

  switch (sel) {
    case KOS_COMPARE_IS:
      if (compare_require_2(sel, count))
        return -1;
      return args[0] == args[1];
    case KOS_COMPARE_IS_NOT:
      if (compare_require_2(sel, count))
        return -1;
      return args[0] != args[1];
    case KOS_COMPARE_IN:
      if (compare_require_2(sel, count))
        return -1;
      return PySequence_Contains(args[1], args[0]);
    case KOS_COMPARE_NOT_IN: {
      if (compare_require_2(sel, count))
        return -1;
      int r = PySequence_Contains(args[1], args[0]);
      return (r < 0) ? -1 : ! r;
    }
    case KOS_COMPARE_IS_INSTANCE:
      if (compare_require_2(sel, count))
        return -1;
      return PyObject_IsInstance(args[0], args[1]);
    case KOS_COMPARE_IS_SUBCLASS:
      if (compare_require_2(sel, count))
        return -1;
      return PyObject_IsSubclass(args[0], args[1]);
    default:
      PyErr_Format(PyExc_SystemError,
                   "bad comparison selector: %i", (int) sel);
      return -1;
  }
}


/* Cold extended tail of kos_compare_reduce. Bool results throughout;
   the isinstance/issubclass count form (value + N classes) builds the
   class tuple the way OBJECT_IS_INSTANCE's PopTail did. */
static KOS_NOINLINE PyObject *compare_reduce_ext(
    KosCompare sel,
    PyObject *const *args,
    size_t count) {

  if (sel == KOS_COMPARE_IS_INSTANCE || sel == KOS_COMPARE_IS_SUBCLASS) {
    PyObject *match;
    if (count < 2) {
      PyErr_Format(PyExc_TypeError, "comparison selector %i requires at"
                   " least 2 positional arguments", (int) sel);
      return NULL;
    } else if (count == 2) {
      match = args[1];
      Py_INCREF(match);
    } else {
      match = PyTuple_New((Py_ssize_t) count - 1);
      if (! match)
        return NULL;
      for (size_t i = 1; i < count; i++) {
        Py_INCREF(args[i]);
        PyTuple_SET_ITEM(match, (Py_ssize_t) i - 1, args[i]);
      }
    }
    int res = (sel == KOS_COMPARE_IS_INSTANCE)
        ? PyObject_IsInstance(args[0], match)
        : PyObject_IsSubclass(args[0], match);
    Py_DECREF(match);
    if (res < 0)
      return NULL;
    return PyBool_FromLong(res);
  }

  int res = kos_compare_chain_ext(sel, args, count);
  if (res < 0)
    return NULL;
  return PyBool_FromLong(res);
}


PyObject *kos_compare_reduce(
    KosCompare sel,
    PyObject *const *args,
    size_t count) {

  if (likely(sel <= KOS_COMPARE_GE))
    return chain_compare((int) sel, compare_names[sel], args, count);

  return compare_reduce_ext(sel, args, count);
}


PyObject *KosOperator_In(PyObject *const *values, size_t count) {

  if (count != 2) {
    PyErr_SetString(PyExc_TypeError, "in requires 2"
                    " positional arguments");
    return NULL;
  }

  int res = PySequence_Contains(values[1], values[0]);
  if (res < 0) {
    return NULL;
  } else if (res) {
    Py_RETURN_TRUE;
  } else {
    Py_RETURN_FALSE;
  }
}


PyObject *KosOperator_NotIn(PyObject *const *values, size_t count) {

  if (count != 2) {
    PyErr_SetString(PyExc_TypeError, "not-in requires 2"
                    " positional arguments");
    return NULL;
  }

  int res = PySequence_Contains(values[1], values[0]);
  if (res < 0) {
    return NULL;
  } else if (res) {
    Py_RETURN_FALSE;
  } else {
    Py_RETURN_TRUE;
  }
}


PyObject *KosOperator_BuildStr(PyObject *const *values, size_t count) {

  if (! count) {
    Py_INCREF(_str__empty);
    return _str__empty;

  } else if (count == 1) {
    PyObject *s = values[0];
    if (PyUnicode_CheckExact(s)) {
      Py_INCREF(s);
    } else {
      s = PyObject_Str(s);
    }
    return s;

  } else {
    PyObject *seq = PyTuple_New(count);
    if (! seq)
      return NULL;

    for (size_t i = 0; i < count; i++) {
      PyObject *s = values[i];

      if (PyUnicode_CheckExact(s)) {
        Py_INCREF(s);

      } else {
        s = PyObject_Str(s);
        if (! s) {
          Py_DECREF(seq);
          return NULL;
        }
      }

      PyTuple_SET_ITEM(seq, i, s);
    }

    PyObject *res = PyUnicode_Join(_str__empty, seq);
    Py_DECREF(seq);

    return res;
  }
}


/* === module method wrappers === */


VAR_FUNC(KosOperator_Add);
VAR_FUNC(KosOperator_BitwiseAnd);
VAR_FUNC(KosOperator_BitwiseOr);
VAR_FUNC(KosOperator_BitwiseXor);
VAR_FUNC(KosOperator_FloorDivide);
VAR_FUNC(KosOperator_MatrixMultiply);
VAR_FUNC(KosOperator_Multiply);
VAR_FUNC(KosOperator_Subtract);
VAR_FUNC(KosOperator_TrueDivide);

VAR_FUNC(KosOperator_In);
VAR_FUNC(KosOperator_NotIn);
VAR_FUNC(KosOperator_BuildStr);

VAR_FUNC(KosOperator_Eq);
VAR_FUNC(KosOperator_Ne);
VAR_FUNC(KosOperator_Lt);
VAR_FUNC(KosOperator_Le);
VAR_FUNC(KosOperator_Gt);
VAR_FUNC(KosOperator_Ge);


static PyMethodDef operators[] = {
  { "+", PICK_FUNC(KosOperator_Add), METHOD_TYPE,
    "(+ VAL *: VALS)" },

  { "&", PICK_FUNC(KosOperator_BitwiseAnd), METHOD_TYPE,
    "(& VAL AND_VAL *: AND_VALS)" },

  { "|", PICK_FUNC(KosOperator_BitwiseOr), METHOD_TYPE,
    "(| VAL OR_VAL *: OR_VALS)" },

  { "^", PICK_FUNC(KosOperator_BitwiseXor), METHOD_TYPE,
    "(^ VAL XOR_VAL *: XOR_VALS)" },

  { "*", PICK_FUNC(KosOperator_Multiply), METHOD_TYPE,
    "(* VAL BY_VAL *: BY_VALS)" },

  { "@", PICK_FUNC(KosOperator_MatrixMultiply), METHOD_TYPE,
    "(@ VAL BY_VAL *: BY_VALS)" },

  { "//", PICK_FUNC(KosOperator_FloorDivide), METHOD_TYPE,
    "(// VAL DENOM *: DENOMS)" },

  { "-", PICK_FUNC(KosOperator_Subtract), METHOD_TYPE,
    "(- VAL *: VALS)" },

  { "/", PICK_FUNC(KosOperator_TrueDivide), METHOD_TYPE,
    "(/ VAL DENOM *: DENOMS)" },

  { "in", PICK_FUNC(KosOperator_In), METHOD_TYPE,
    "(in VAL CONTAINER)" },

  { "not-in", PICK_FUNC(KosOperator_NotIn), METHOD_TYPE,
    "(not-in VAL CONTAINER)" },

  { "build-str", PICK_FUNC(KosOperator_BuildStr), METHOD_TYPE,
    "(build-str VAL *: VALS)" },

  { "==", PICK_FUNC(KosOperator_Eq), METHOD_TYPE,
    "(== VAL1 VAL2 *: VALS)" },

  { "!=", PICK_FUNC(KosOperator_Ne), METHOD_TYPE,
    "(!= VAL1 VAL2 *: VALS)" },

  { "<", PICK_FUNC(KosOperator_Lt), METHOD_TYPE,
    "(< VAL1 VAL2 *: VALS)" },

  { "<=", PICK_FUNC(KosOperator_Le), METHOD_TYPE,
    "(<= VAL1 VAL2 *: VALS)" },

  { ">", PICK_FUNC(KosOperator_Gt), METHOD_TYPE,
    "(> VAL1 VAL2 *: VALS)" },

  { ">=", PICK_FUNC(KosOperator_Ge), METHOD_TYPE,
    "(>= VAL1 VAL2 *: VALS)" },

  { NULL, },
};


/* === unit lifecycle === */


int koskari_operator_init(PyObject *mod) {

  if (! mod)
    return -1;

  STR_CONST(_str__empty, "");

  for (PyMethodDef *fdef = operators; fdef->ml_name; fdef++) {

    PyObject *func = PyCFunction_New(fdef, mod);
    if (! func)
      return -1;

    int res = PyObject_SetAttrString(mod, fdef->ml_name, func);
    Py_DECREF(func);

    if (res)
      return -1;
  }

  return 0;
}


int koskari_operator_clear(PyObject *mod) {

  Py_CLEAR(_str__empty);

  return 0;
}


// The end.
