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
   VAR: arg = constant index of a name. Look up that name in the current
   frame's namespace and push the value (or await it if async).
*/

KOS_OP_HANDLER(VAR) {
  PyObject *name = get_name_from_insn(CURRENT_CODE(ctx), insn);
  if (! name)
    return -1;
  PyObject *val = frame_ns_deref(ctx, name);
  if (! val)
    return -1;
  return push_result(ctx, val);
}
