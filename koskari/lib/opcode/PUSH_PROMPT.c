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
   PUSH_PROMPT: arg = constant index of prompt_id. Push a delimited prompt
   onto the dynamic chain for shift/reset.
*/

KOS_OP_HANDLER(PUSH_PROMPT) {
  uint32_t prompt_id;
  if (get_prompt_id_from_insn(CURRENT_CODE(ctx), insn, &prompt_id) < 0)
    return -1;
  if (! kos_dyn_push_prompt(ctx, prompt_id))
    return -1;
  (void) out_ev;
  return 0;
}
