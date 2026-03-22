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
   CALL_FAST_NAMES: arg = extra index. Schema FASTVAR (slot), CONST (kw
   names), COUNT (npos). Call callable from fast local slot with keyword
   args; not yet implemented.
*/

KOS_OP_HANDLER(CALL_FAST_NAMES) {
  return op_unimplemented_stub(ctx, out_ev, insn);
}


// The end.
