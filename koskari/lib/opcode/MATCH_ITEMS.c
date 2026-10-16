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
  MATCH_ITEMS: no arg. Push an empty list, the items a repeat's
  iterations append to, which sits under the repeat's mark while the
  passes run (design/interpreter/matcher-bytecode.md).
*/
KOS_OP_HANDLER(MATCH_ITEMS) {
  KOS_SYNC_PC(es, insn);
  (void) kos_op_delivery_;
  PyObject *list = PyList_New(0);
  if (unlikely(! list))
    RESULT_RAISE();
  RESULT_STEAL(es, list);
}


// The end.
