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
   BUILD_LIST_CONST: arg = constant index of a sequence (typically a pooled
   tuple of constant elements). Push a fresh list built from that sequence.

   Each evaluation allocates a new list, so the mutable result never aliases
   the immutable pool constant, which is resolved onto the record at
   finalize.
*/

KOS_OP_HANDLER(BUILD_LIST_CONST) {
  KOS_SYNC_PC(es, insn);
  PyObject *seq = KosOp_OBJ(insn);

  PyObject *built = PySequence_List(seq);
  if (unlikely(! built))
    RESULT_RAISE();

  RESULT_STEAL(es, built);
}


// The end.
