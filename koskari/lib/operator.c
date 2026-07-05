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


static PyObject *_str__empty = NULL;


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


static inline PyObject *reduce_method(
    const char *name,
    binaryfunc func,
    unaryfunc solofunc,
    PyObject *const *args,
    size_t count) {

  if (count == 0) {
    if (solofunc) {
      PyErr_Format(PyExc_TypeError, "%s requires at least 1"
                   " positional argument", name);
    } else {
      PyErr_Format(PyExc_TypeError, "%s requires at least 2"
                   " positional arguments", name);
    }
    return NULL;
  }

  if (count == 1) {
    if (solofunc) {
      return solofunc(args[0]);

    } else {
      PyErr_Format(PyExc_TypeError, "%s requires at least 2"
                   " positional arguments", name);
      return NULL;
    }
  }

  // value is our accumulator, so we own it to start
  PyObject *value = args[0];
  Py_INCREF(value);

  for (size_t i = 1; i < (size_t) count; i++) {
    PyObject *n = func(value, args[i]);
    Py_DECREF(value);

    value = n;
    if (! value)
      break;
  }

  return value;
}


PyObject *KosOperator_Add(PyObject *const *values, size_t count) {

  return reduce_method("+", PyNumber_Add, PyNumber_Positive,
                       values, count);
}


PyObject *KosOperator_BitwiseAnd(PyObject *const *values, size_t count) {

  return reduce_method("&", PyNumber_And, NULL, values, count);
}


PyObject *KosOperator_BitwiseOr(PyObject *const *values, size_t count) {

  return reduce_method("|", PyNumber_Or, NULL, values, count);
}


PyObject *KosOperator_BitwiseXor(PyObject *const *values, size_t count) {

  return reduce_method("^", PyNumber_Xor, NULL, values, count);
}


PyObject *KosOperator_FloorDivide(PyObject *const *values, size_t count) {

  return reduce_method("//", PyNumber_FloorDivide, NULL, values, count);
}


PyObject *KosOperator_MatrixMultiply(PyObject *const *values, size_t count) {

  return reduce_method("@", PyNumber_MatrixMultiply, NULL, values, count);
}


PyObject *KosOperator_Multiply(PyObject *const *values, size_t count) {

  return reduce_method("*", PyNumber_Multiply, NULL, values, count);
}


PyObject *KosOperator_Subtract(PyObject *const *values, size_t count) {

  return reduce_method("-", PyNumber_Subtract, PyNumber_Negative,
                       values, count);
}


PyObject *KosOperator_TrueDivide(PyObject *const *values, size_t count) {

  return reduce_method("/", PyNumber_TrueDivide, NULL, values, count);
}


/* === in-place reducing operators === */

PyObject *KosOperator_InplaceAdd(PyObject *const *values, size_t count) {

  return reduce_method("+=", PyNumber_InPlaceAdd, NULL, values, count);
}


PyObject *KosOperator_InplaceBitwiseAnd(
    PyObject *const *values, size_t count) {

  return reduce_method("&=", PyNumber_InPlaceAnd, NULL, values, count);
}


PyObject *KosOperator_InplaceBitwiseOr(PyObject *const *values, size_t count) {

  return reduce_method("|=", PyNumber_InPlaceOr, NULL, values, count);
}


PyObject *KosOperator_InplaceBitwiseXor(
    PyObject *const *values, size_t count) {

  return reduce_method("^=", PyNumber_InPlaceXor, NULL, values, count);
}


PyObject *KosOperator_InplaceFloorDivide(
    PyObject *const *values, size_t count) {

  return reduce_method("//=", PyNumber_InPlaceFloorDivide, NULL,
                       values, count);
}


PyObject *KosOperator_InplaceLshift(PyObject *const *values, size_t count) {

  return reduce_method("<<=", PyNumber_InPlaceLshift, NULL, values, count);
}


PyObject *KosOperator_InplaceMatrixMultiply(
    PyObject *const *values, size_t count) {

  return reduce_method("@=", PyNumber_InPlaceMatrixMultiply, NULL,
                       values, count);
}


PyObject *KosOperator_InplaceMultiply(PyObject *const *values, size_t count) {

  return reduce_method("*=", PyNumber_InPlaceMultiply, NULL, values, count);
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

  return reduce_method("%=", PyNumber_InPlaceRemainder, NULL, values, count);
}


PyObject *KosOperator_InplaceRshift(PyObject *const *values, size_t count) {

  return reduce_method(">>=", PyNumber_InPlaceRshift, NULL, values, count);
}


PyObject *KosOperator_InplaceSubtract(PyObject *const *values, size_t count) {

  return reduce_method("-=", PyNumber_InPlaceSubtract, NULL, values, count);
}


PyObject *KosOperator_InplaceTrueDivide(
    PyObject *const *values, size_t count) {

  return reduce_method("/=", PyNumber_InPlaceTrueDivide, NULL, values, count);
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
