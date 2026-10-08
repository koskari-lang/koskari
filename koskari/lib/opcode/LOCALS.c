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
  LOCALS: arg unused. Push the current frame's namespace (locals) onto
  the stack. Reifying the namespace makes it storable, so it is
  revealed to the cyclic collector here.
*/
KOS_OP_HANDLER(LOCALS) {
  KOS_SYNC_PC(es, insn);
  PyObject *ns = KosEvalState_RevealNamespace(es);
  RESULT_STEAL(es, ns);
}


// The end.
