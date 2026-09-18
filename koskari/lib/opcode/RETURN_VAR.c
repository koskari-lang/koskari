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
  RETURN_VAR: arg = pool index of a name; return the value the name
  resolves to in the current frame's namespace, raising NameError when
  it is unbound, as VAR would.  The fusion of VAR name, RETURN 1; lowers
  to RETURN_FAST when the name has a fast slot.  Stack unchanged.
*/
KOS_OP_HANDLER(RETURN_VAR) {
  PyObject *val = frame_ns_deref(es, KosOp_OBJ(insn));
  if (unlikely(! val))
    RESULT_RAISE();
  TAILCALL(kos_return_deliver, es, insn, val);
}


// The end.
