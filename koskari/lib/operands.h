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


/**
   koskari/lib/operands.h

   Single source of truth for the parameterized arithmetic operator set used
   by the OBJECT_OPERAND / *_OBJECT_INPLACE opcode families. KOS_OPERAND_LIST(X)
   expands each operator as X(NAME, int-fast-op, reduce-fn, in-place-fn); the
   KosOperand enum and the per-family switch bodies are generated from it so
   adding an operator is one row.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GPL v.3
*/


#ifndef KOSKARI_OPERANDS_H
#define KOSKARI_OPERANDS_H


#include <Python.h>

#include "longops.h"


/* Ternary power needs binary shims to fit the uniform binaryfunc columns. */
static inline PyObject *kos_operand_power(PyObject *o, PyObject *r) {
  return PyNumber_Power(o, r, Py_None);
}

static inline PyObject *kos_operand_inplace_power(PyObject *o, PyObject *r) {
  return PyNumber_InPlacePower(o, r, Py_None);
}


/* X(NAME, int-fast KosLongBinop (or KOS_LBOP_NONE), reduce fn, in-place fn) */
#define KOS_OPERAND_LIST(X)                                                   \
  X(ADD,             KOS_LBOP_ADD,       PyNumber_Add,       PyNumber_InPlaceAdd)      \
  X(SUBTRACT,        KOS_LBOP_SUBTRACT,  PyNumber_Subtract,  PyNumber_InPlaceSubtract) \
  X(MULTIPLY,        KOS_LBOP_MULTIPLY,  PyNumber_Multiply,  PyNumber_InPlaceMultiply) \
  X(MODULO,          KOS_LBOP_MODULO,    PyNumber_Remainder, PyNumber_InPlaceRemainder) \
  X(BIT_AND,         KOS_LBOP_BIT_AND,   PyNumber_And,       PyNumber_InPlaceAnd)      \
  X(BIT_OR,          KOS_LBOP_BIT_OR,    PyNumber_Or,        PyNumber_InPlaceOr)       \
  X(BIT_XOR,         KOS_LBOP_BIT_XOR,   PyNumber_Xor,       PyNumber_InPlaceXor)      \
  X(BIT_LEFT,        KOS_LBOP_BIT_LEFT,  PyNumber_Lshift,    PyNumber_InPlaceLshift)   \
  X(BIT_RIGHT,       KOS_LBOP_BIT_RIGHT, PyNumber_Rshift,    PyNumber_InPlaceRshift)   \
  X(TRUE_DIVIDE,     KOS_LBOP_NONE,      PyNumber_TrueDivide,  PyNumber_InPlaceTrueDivide)  \
  X(FLOOR_DIVIDE,    KOS_LBOP_NONE,      PyNumber_FloorDivide, PyNumber_InPlaceFloorDivide) \
  X(MATRIX_MULTIPLY, KOS_LBOP_NONE,      PyNumber_MatrixMultiply, PyNumber_InPlaceMatrixMultiply) \
  X(POWER,           KOS_LBOP_NONE,      kos_operand_power,    kos_operand_inplace_power)


typedef enum KosOperand {
#define KOS_OPERAND_ENUM(NAME, LOP, RFN, IFN) KOS_OPERAND_##NAME,
  KOS_OPERAND_LIST(KOS_OPERAND_ENUM)
#undef KOS_OPERAND_ENUM
  KOS_OPERAND_COUNT   /* number of operators; also the invalid sentinel */
} KosOperand;


#endif


// The end.
