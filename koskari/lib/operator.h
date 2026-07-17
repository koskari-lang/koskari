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

/* chain-evaluate; 1 = held, 0 = failed (branch), -1 = error. The
   richcompare six stay inline at the call site -- the selector IS the
   Py_LT..Py_GE ordinal, passed through untranslated -- so the dispatch
   loop's check path keeps the pre-unification inlining. */
static inline int kos_compare_chain(
    KosCompare sel,
    PyObject *const *args,
    size_t count) {

  if (likely(sel <= KOS_COMPARE_GE)) {
    int res = 1;
    for (size_t i = 1; i < count; i++) {
      res = PyObject_RichCompareBool(args[i - 1], args[i], (int) sel);
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
