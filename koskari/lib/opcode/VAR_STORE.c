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
   VAR_STORE: arg = constant index (name). Pop TOS and store into name in
   the current frame namespace.
*/

KOS_OP_HANDLER(VAR_STORE) {
  PyObject *name = get_name_from_insn(KosEvalState_CurrentCode(es), insn);
  if (! name)
    return EVAL_RAISE();
  PyObject *val = KosEvalState_StackPopRef(es);
  if (! val)
    return EVAL_RAISE();
  if (frame_ns_store(es, name, val) < 0) {
    Py_DECREF(val);
    return EVAL_RAISE();
  }
  Py_DECREF(val);
  return EVAL_NORMAL();
}


// The end.
