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
   BUILD_KWDS: arg = pair count (number of keyword pairs).
   Stack: [..., k_0, v_0, ..., k_{n-1}, v_{n-1}] -> [..., dict].
   Pop 2*opa elements as key,value pairs and push a dict.
*/

KOS_OP_HANDLER(BUILD_KWDS) {
  KOS_SYNC_PC(es, insn);
  unsigned opa = KosOp_ARG(insn);
  PyObject *built = KosEvalState_StackPopTailDict(es, (size_t) opa);

  if (! built)
    RESULT_RAISE();

  RESULT_STEAL(es, built);
}


// The end.
