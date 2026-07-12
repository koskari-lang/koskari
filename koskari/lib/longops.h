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


#ifndef KOSKARI_LONGOPS_H
#define KOSKARI_LONGOPS_H


/**
   longops.h

   Two-operand PyLong fast path for the arithmetic and bitwise opcode
   handlers.  When both operands are exact ints that fit in a C long
   long and the operation cannot lose precision, the result is computed
   directly, skipping the PyNumber binary-slot dispatch and the digit
   loops in longobject.  Anything else falls back to the generic path,
   which recomputes from the original operands.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include <Python.h>
#include <limits.h>

#include "hints.h"


typedef enum KosLongBinop {
  KOS_LBOP_ADD,
  KOS_LBOP_SUBTRACT,
  KOS_LBOP_MULTIPLY,
  KOS_LBOP_MODULO,
  KOS_LBOP_BIT_AND,
  KOS_LBOP_BIT_OR,
  KOS_LBOP_BIT_XOR,
  KOS_LBOP_BIT_LEFT,
  KOS_LBOP_BIT_RIGHT,
} KosLongBinop;


/**
   Try to compute l <op> r directly as C long long values.

   Returns 1 with *out set to a new reference when handled, 0 when the
   operands or result do not qualify (caller runs the generic path; *out
   is untouched), or -1 with an error set on allocation failure.

   Semantics notes.  Bitwise AND/OR/XOR on two's-complement long long
   match Python int semantics exactly for any in-range operands, sign
   included.  Shifts are restricted to non-negative operands: C >> on a
   negative left operand is implementation-defined, and a negative shift
   count must raise ValueError, so both cases defer to the generic path.
   Modulo is restricted to a non-negative dividend and a positive
   divisor so C % matches Python remainder; zero or negative operands
   fall back (ZeroDivisionError and sign-of-divisor remainder).
   Overflow on +, -, *, and << falls back rather than widening.
*/
static inline int kos_long_binop_fast(
    KosLongBinop op,
    PyObject *l,
    PyObject *r,
    PyObject **out) {

  long long a, b, res;
  int overflow;

  if (! (PyLong_CheckExact(l) && PyLong_CheckExact(r)))
    return 0;

  /* CheckExact passed, so these cannot raise; only overflow signals. */
  a = PyLong_AsLongLongAndOverflow(l, &overflow);
  if (unlikely(overflow))
    return 0;
  b = PyLong_AsLongLongAndOverflow(r, &overflow);
  if (unlikely(overflow))
    return 0;

  switch (op) {
  case KOS_LBOP_ADD:
    if (unlikely(__builtin_add_overflow(a, b, &res)))
      return 0;
    break;

  case KOS_LBOP_SUBTRACT:
    if (unlikely(__builtin_sub_overflow(a, b, &res)))
      return 0;
    break;

  case KOS_LBOP_MULTIPLY:
    if (unlikely(__builtin_mul_overflow(a, b, &res)))
      return 0;
    break;

  case KOS_LBOP_MODULO:
    if (unlikely(a < 0 || b <= 0))
      return 0;
    res = a % b;
    break;

  case KOS_LBOP_BIT_AND:
    res = a & b;
    break;

  case KOS_LBOP_BIT_OR:
    res = a | b;
    break;

  case KOS_LBOP_BIT_XOR:
    res = a ^ b;
    break;

  case KOS_LBOP_BIT_LEFT:
    if (unlikely(a < 0 || b < 0 || b >= 63))
      return 0;
    if (unlikely(a != 0 && a > (LLONG_MAX >> b)))
      return 0;
    res = a << b;
    break;

  case KOS_LBOP_BIT_RIGHT:
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


#endif


// The end.
