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
  DUP: arg = number of top stack slots to duplicate. Duplicate that many
  top-of-stack values (pushed again in order).
*/
KOS_OP_HANDLER(DUP) {
  int opa = (int) KosOp_ARG(insn);
  if (likely(opa == 1)) {
    if (unlikely(KosEvalState_StackDup1(es) < 0))
      RESULT_RAISE_AT(es, insn);
    RESULT_NEXT(es);
  }
  if (unlikely(opa < 0)) {
    PyErr_Format(PyExc_RuntimeError, "DUP invoked with negative"
                 " argument count: %i", opa);
    RESULT_RAISE_AT(es, insn);
  }

  if (unlikely(KosEvalState_StackDuplicate(es, (size_t) opa) < 0))
    RESULT_RAISE_AT(es, insn);

  RESULT_NEXT(es);
}


// The end.
