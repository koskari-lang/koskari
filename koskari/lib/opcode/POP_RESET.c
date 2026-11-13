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
   POP_RESET: arg unused. Closes a PUSH_RESET extent. The frame the
   reset is written in stands here while the body runs, and goes on
   from here once the reset's value has been pushed on its stack. The
   body's own frame never reaches it.
*/

KOS_OP_HANDLER(POP_RESET) {
  RESULT_NEXT(ctx);
}


// The end.
