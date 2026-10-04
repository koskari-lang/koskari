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
  VAR_STORE_CONST: arg = extra index. Schema VAR, CONST for name and value.
  Store the value into the name in the current frame namespace.
*/
KOS_OP_HANDLER(VAR_STORE_CONST) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *name = KosExtra_OBJ(row, 0);
  PyObject *val = KosExtra_OBJ(row, 1);

  if (unlikely(frame_ns_store(es, name, val) < 0)) {
    RESULT_RAISE();
  }

  RESULT_NEXT(es);
}


// The end.
