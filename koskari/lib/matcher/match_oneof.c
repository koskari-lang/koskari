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
  Included by matcher.c. The ONEOF match step and its width.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === ONEOF matching === */


static void width_oneof(KosMatcher *m, KosMatchNode *n) {
  uint32_t mn = KOS_MATCH_UNBOUNDED, mx = 0;
  int has_unbounded = 0;
  int has_branch = 0;
  for (uint32_t cidx = n->as.oneof.first_branch;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = m->nodes[cidx].next) {
    has_branch = 1;
    uint32_t cmn = m->nodes[cidx].min_width;
    uint32_t cmx = m->nodes[cidx].max_width;
    if (cmn < mn)
      mn = cmn;
    if (cmx == KOS_MATCH_UNBOUNDED)
      has_unbounded = 1;
    else if (cmx > mx)
      mx = cmx;
  }
  if (! has_branch) {
    mn = 0;
    mx = 0;
  }
  n->min_width = mn;
  n->max_width = has_unbounded ? KOS_MATCH_UNBOUNDED : mx;
}


static int match_oneof(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  for (uint32_t cidx = n->as.oneof.first_branch;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    Py_ssize_t snap = engine_snapshot(e);
    Py_ssize_t saved = *cursor;
    int r = match_node(e, cidx, cursor, trailing_min);
    if (r == 1)
      return 1;
    if (r < 0)
      return -1;
    *cursor = saved;
    engine_rollback(e, snap);
  }
  return 0;
}

// The end.
