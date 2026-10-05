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
  Included by matcher.c. The NOT match step.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === NOT matching === */


static int match_not(
    MatchEngine *e,
    const KosMatchNode *n,
    Py_ssize_t *cursor) {

  Py_ssize_t snap = engine_snapshot(e);
  Py_ssize_t saved = *cursor;
  int r = match_node(e, n->as.not_.subnode, cursor, 0);
  *cursor = saved;
  engine_rollback(e, snap);
  if (r < 0)
    return -1;
  return r ? 0 : 1;
}

// The end.
