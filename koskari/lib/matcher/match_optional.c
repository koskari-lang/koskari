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
  Included by matcher.c. The OPTIONAL match step and its width.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === OPTIONAL matching === */


static void width_optional(KosMatcher *m, KosMatchNode *n) {
  uint32_t cmx = 1;
  if (n->as.optional.subnode != KOS_MATCH_NO_INDEX)
    cmx = m->nodes[n->as.optional.subnode].max_width;
  n->min_width = 0;
  n->max_width = cmx;
}


static int match_optional(
    MatchEngine *e,
    const KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t snap = engine_snapshot(e);
  Py_ssize_t saved = *cursor;
  int r = match_node(e, n->as.optional.subnode, cursor, trailing_min);
  if (r == 1)
    return 1;
  if (r < 0)
    return -1;
  *cursor = saved;
  engine_rollback(e, snap);
  return 1;
}


// The end.
