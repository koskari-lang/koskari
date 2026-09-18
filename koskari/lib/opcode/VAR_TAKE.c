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
  VAR_TAKE: arg = constant index (name). Deref name, clear binding,
  push value.
*/
KOS_OP_HANDLER(VAR_TAKE) {
  PyObject *name = KosOp_OBJ(insn);

  PyObject *val = frame_ns_take(es, name);
  if (unlikely(! val))
    RESULT_RAISE();

  RESULT_STEAL(es, val);
}


// The end.
