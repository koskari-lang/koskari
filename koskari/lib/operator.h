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
