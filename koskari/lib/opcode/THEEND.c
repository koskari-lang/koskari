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
   THEEND: arg unused. End-of-code sentinel; has no runtime implementation.
   If execution reaches THEEND, raise a hardcoded unimplemented error.
*/

KOS_OP_HANDLER(THEEND) {
  KosContext *ctx = KosEvalState_Context(es);
  (void) ctx;
  (void) out_ev;
  (void) insn;
  PyErr_SetString(PyExc_RuntimeError, "THEEND has no runtime implementation");
  return KOS_OP_POST_ERR();
}
