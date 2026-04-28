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
   VAR_CLEAR: arg = constant index (name). Delete name from namespace.
*/

KOS_OP_HANDLER(VAR_CLEAR) {
  PyObject *name = get_name_from_insn(KosEvalState_CurrentCode(es), insn);
  if (! name)
    return KOS_OP_POST_ERR();
  if (frame_ns_clear(es, name) < 0)
    return KOS_OP_POST_ERR();
  (void) out_ev;
  return KOS_OP_RUN();
}
