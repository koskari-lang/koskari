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


/* Two-operand PyLong fast path, keyed directly on KosOperand (no separate
   selector enum). When both operands are exact ints that fit in a C long long
   and the operation cannot lose precision, compute it directly and skip the
   PyNumber slot dispatch and longobject digit loops; anything else returns 0
   so the caller runs the protocol fn on the original operands. Returns 1 with
   *out set (new ref) when handled, 0 when declined (*out untouched), -1 with
   an error set on allocation failure.

   Bitwise AND/OR/XOR on two's-complement match Python int semantics for any
   in-range operands. Shifts require non-negative operands (C >> on a negative
   is implementation-defined; a negative count must raise). Modulo requires a
   non-negative dividend and positive divisor so C % matches Python remainder.
   Overflow on +, -, *, << declines rather than widening. Divide, matmul, and
   power have no fast path and hit the default. */
static inline int kos_long_binop_fast(
    KosOperand op,
    PyObject *l,
    PyObject *r,
    PyObject **out) {

  if (! (PyLong_CheckExact(l) && PyLong_CheckExact(r)))
    return 0;

  int overflow;
  long long a = PyLong_AsLongLongAndOverflow(l, &overflow);
  if (unlikely(overflow))
    return 0;
  long long b = PyLong_AsLongLongAndOverflow(r, &overflow);
  if (unlikely(overflow))
    return 0;

  long long res;
  switch (op) {
  case KOS_OPERAND_ADD:
    if (unlikely(__builtin_add_overflow(a, b, &res)))
      return 0;
    break;
  case KOS_OPERAND_SUBTRACT:
    if (unlikely(__builtin_sub_overflow(a, b, &res)))
      return 0;
    break;
  case KOS_OPERAND_MULTIPLY:
    if (unlikely(__builtin_mul_overflow(a, b, &res)))
      return 0;
    break;
  case KOS_OPERAND_MODULO:
    if (unlikely(a < 0 || b <= 0))
      return 0;
    res = a % b;
    break;
  case KOS_OPERAND_BIT_AND:
    res = a & b;
    break;
  case KOS_OPERAND_BIT_OR:
    res = a | b;
    break;
  case KOS_OPERAND_BIT_XOR:
    res = a ^ b;
    break;
  case KOS_OPERAND_BIT_LEFT:
    if (unlikely(a < 0 || b < 0 || b >= 63))
      return 0;
    if (unlikely(a != 0 && a > (LLONG_MAX >> b)))
      return 0;
    res = a << b;
    break;
  case KOS_OPERAND_BIT_RIGHT:
    if (unlikely(a < 0 || b < 0))
      return 0;
    res = (b >= 63) ? 0 : (a >> b);
    break;
  default:
    return 0;
  }

  *out = PyLong_FromLongLong(res);
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


/* Cold path for reduce_method: the arity edges (0, 1) and the true reduce
   (count > 2). Kept out of line so the inlined hot path stays tiny. */
static KOS_NOINLINE PyObject *reduce_slow(
    const char *name,
    KosOperand op,
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
    if (kos_long_binop_fast(op, value, args[i], &n) == 0)
      n = func(value, args[i]);
    Py_DECREF(value);
    value = n;
    if (! value)
      break;
  }

  return value;
}


/* Reducing operator. The overwhelmingly common two-operand case is a tiny
   always-inlined body: try the PyLong fast path (the op-type switch folds away
   at each call site, where op is constant), else the protocol fn -- with no
   accumulator INCREF/DECREF. Every other arity tails to the cold reduce_slow. */
KOS_OP_INLINE PyObject *reduce_method(
    const char *name,
    KosOperand op,
    binaryfunc func,
    unaryfunc solofunc,
    PyObject *const *args,
    size_t count) {

  if (likely(count == 2)) {
    PyObject *out = NULL;
    if (kos_long_binop_fast(op, args[0], args[1], &out) == 0)
      out = func(args[0], args[1]);
    return out;
  }

  return reduce_slow(name, op, func, solofunc, args, count);
}


PyObject *KosOperator_Add(PyObject *const *values, size_t count) {

  return reduce_method("+", KOS_OPERAND_ADD, PyNumber_Add, PyNumber_Positive,
                       values, count);
}


PyObject *KosOperator_BitwiseAnd(PyObject *const *values, size_t count) {

  return reduce_method("&", KOS_OPERAND_BIT_AND, PyNumber_And, NULL,
                       values, count);
}


PyObject *KosOperator_BitwiseOr(PyObject *const *values, size_t count) {

  return reduce_method("|", KOS_OPERAND_BIT_OR, PyNumber_Or, NULL,
                       values, count);
}


PyObject *KosOperator_BitwiseXor(PyObject *const *values, size_t count) {

  return reduce_method("^", KOS_OPERAND_BIT_XOR, PyNumber_Xor, NULL,
                       values, count);
}


PyObject *KosOperator_FloorDivide(PyObject *const *values, size_t count) {

  return reduce_method("//", KOS_OPERAND_FLOOR_DIVIDE, PyNumber_FloorDivide, NULL,
                       values, count);
}


PyObject *KosOperator_MatrixMultiply(PyObject *const *values, size_t count) {

  return reduce_method("@", KOS_OPERAND_MATRIX_MULTIPLY, PyNumber_MatrixMultiply, NULL,
                       values, count);
}


PyObject *KosOperator_Multiply(PyObject *const *values, size_t count) {

  return reduce_method("*", KOS_OPERAND_MULTIPLY, PyNumber_Multiply, NULL,
                       values, count);
}


PyObject *KosOperator_Subtract(PyObject *const *values, size_t count) {

  return reduce_method("-", KOS_OPERAND_SUBTRACT, PyNumber_Subtract,
                       PyNumber_Negative, values, count);
}


PyObject *KosOperator_TrueDivide(PyObject *const *values, size_t count) {

  return reduce_method("/", KOS_OPERAND_TRUE_DIVIDE, PyNumber_TrueDivide, NULL,
                       values, count);
}


/* Binary-only arithmetic operators (%, <<, >>): reducing in shape but the
   compiler only ever emits them with two operands. Not exposed as operator-
   module functions; used by the OBJECT_OPERAND dispatch. */

PyObject *KosOperator_Modulo(PyObject *const *values, size_t count) {

  return reduce_method("%", KOS_OPERAND_MODULO, PyNumber_Remainder, NULL,
                       values, count);
}


PyObject *KosOperator_Lshift(PyObject *const *values, size_t count) {

  return reduce_method("<<", KOS_OPERAND_BIT_LEFT, PyNumber_Lshift, NULL,
                       values, count);
}


PyObject *KosOperator_Rshift(PyObject *const *values, size_t count) {

  return reduce_method(">>", KOS_OPERAND_BIT_RIGHT, PyNumber_Rshift, NULL,
                       values, count);
}


/* Power is 2-arg (base, exp) or 3-arg (base, exp, mod); not a reduce. */

PyObject *KosOperator_Power(PyObject *const *values, size_t count) {

  if (count == 2)
    return PyNumber_Power(values[0], values[1], Py_None);
  if (count == 3)
    return PyNumber_Power(values[0], values[1], values[2]);

  PyErr_SetString(PyExc_TypeError,
                  "** requires 2 or 3 positional arguments");
  return NULL;
}


/* === in-place reducing operators === */

PyObject *KosOperator_InplaceAdd(PyObject *const *values, size_t count) {

  return reduce_method("+=", KOS_OPERAND_ADD, PyNumber_InPlaceAdd, NULL,
                       values, count);
}


PyObject *KosOperator_InplaceBitwiseAnd(
    PyObject *const *values, size_t count) {

  return reduce_method("&=", KOS_OPERAND_BIT_AND, PyNumber_InPlaceAnd, NULL,
                       values, count);
}


PyObject *KosOperator_InplaceBitwiseOr(PyObject *const *values, size_t count) {

  return reduce_method("|=", KOS_OPERAND_BIT_OR, PyNumber_InPlaceOr, NULL,
                       values, count);
}


PyObject *KosOperator_InplaceBitwiseXor(
    PyObject *const *values, size_t count) {

  return reduce_method("^=", KOS_OPERAND_BIT_XOR, PyNumber_InPlaceXor, NULL,
                       values, count);
}


PyObject *KosOperator_InplaceFloorDivide(
    PyObject *const *values, size_t count) {

  return reduce_method("//=", KOS_OPERAND_FLOOR_DIVIDE, PyNumber_InPlaceFloorDivide,
                       NULL, values, count);
}


PyObject *KosOperator_InplaceLshift(PyObject *const *values, size_t count) {

  return reduce_method("<<=", KOS_OPERAND_BIT_LEFT, PyNumber_InPlaceLshift,
                       NULL, values, count);
}


PyObject *KosOperator_InplaceMatrixMultiply(
    PyObject *const *values, size_t count) {

  return reduce_method("@=", KOS_OPERAND_MATRIX_MULTIPLY, PyNumber_InPlaceMatrixMultiply,
                       NULL, values, count);
}


PyObject *KosOperator_InplaceMultiply(PyObject *const *values, size_t count) {

  return reduce_method("*=", KOS_OPERAND_MULTIPLY, PyNumber_InPlaceMultiply,
                       NULL, values, count);
}


PyObject *KosOperator_InplacePower(PyObject *const *values, size_t count) {

  // strictly binary, no reduce; PyNumber_InPlacePower is ternary but
  // the augmented form has no modulus
  if (count != 2) {
    PyErr_SetString(PyExc_TypeError, "**= requires exactly 2"
                    " positional arguments");
    return NULL;
  }

  return PyNumber_InPlacePower(values[0], values[1], Py_None);
}


PyObject *KosOperator_InplaceRemainder(PyObject *const *values, size_t count) {

  return reduce_method("%=", KOS_OPERAND_MODULO, PyNumber_InPlaceRemainder,
                       NULL, values, count);
}


PyObject *KosOperator_InplaceRshift(PyObject *const *values, size_t count) {

  return reduce_method(">>=", KOS_OPERAND_BIT_RIGHT, PyNumber_InPlaceRshift,
                       NULL, values, count);
}


PyObject *KosOperator_InplaceSubtract(PyObject *const *values, size_t count) {

  return reduce_method("-=", KOS_OPERAND_SUBTRACT, PyNumber_InPlaceSubtract,
                       NULL, values, count);
}


PyObject *KosOperator_InplaceTrueDivide(
    PyObject *const *values, size_t count) {

  return reduce_method("/=", KOS_OPERAND_TRUE_DIVIDE, PyNumber_InPlaceTrueDivide,
                       NULL, values, count);
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
