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
  BUILD_ARGS: arg = count of positional arguments.
  Stack: [..., v_0, ..., v_{n-1}] -> [..., tuple].
  Pop n elements and push a tuple of them.
*/
KOS_OP_HANDLER(BUILD_ARGS) {
  unsigned opa = KosOp_ARG(insn);
  PyObject *built = KosEvalState_StackPopTail(es, (size_t) opa);

  if (unlikely(! built))
    RESULT_RAISE();

  RESULT_STEAL(es, built);
}


// The end.
