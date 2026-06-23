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
   POP_CC: arg unused. Pop the CC block from the dynamic chain. The next
   instruction is the continuation's resume point.
*/

KOS_OP_HANDLER(POP_CC) {
  (void) insn;

  KosDynNode *top = KosEvalState_CurrentNode(es);

  if (unlikely(kos_dyn_pop_cc(es, (KosDynCC *) top) < 0))
    return EVAL_RAISE();

  return EVAL_NORMAL();
}


// The end.
