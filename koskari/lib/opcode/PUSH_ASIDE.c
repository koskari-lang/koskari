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
   PUSH_ASIDE: arg = extra index. Schema CONST (a NamespaceSpec, or
   None), ASIDE (the name the aside is declared under).

   Declares an aside: the instructions from the next one up to their
   RETURN, closed by POP_ASIDE. They run in a frame of their own over
   this frame's code, made by the instruction that names the aside
   (RESET, ABORT, SHIFT): over a namespace built from the spec, whose
   closure names are resolved from the namespace live at that
   instruction, or with None over that namespace itself. The frame the
   aside is written in runs none of them: it goes on at the
   instruction after the POP_ASIDE.
*/

KOS_OP_HANDLER(PUSH_ASIDE) {
  RESULT_JUMP_AT(ctx, KosExtra_ASIDE(KosOp_ROW(insn), 1)->after);
}


// The end.
