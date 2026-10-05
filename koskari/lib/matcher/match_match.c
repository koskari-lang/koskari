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
  Included by matcher.c. The MATCH match step and its width.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === MATCH matching === */


static void width_match(KosMatcher *m, KosMatchNode *n) {
  chain_width(m, n->as.match.first_child, &n->min_width, &n->max_width);
}


static int match_match(
    MatchEngine *e,
    const KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  /* Sum of min_widths of all children, drained as we iterate to
     compute each child's trailing. UNBOUNDED min would mean a child
     reserves arbitrary spine; treat as 0 for trailing accounting (the
     validator already enforces post-REST suffixes are deterministic). */
  uint32_t remaining = 0;
  for (uint32_t cidx = n->as.match.first_child;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    uint32_t cm = e->m->nodes[cidx].min_width;
    if (cm != KOS_MATCH_UNBOUNDED)
      remaining = saturating_add(remaining, cm);
  }

  for (uint32_t cidx = n->as.match.first_child;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    KosMatchNode *const child = &e->m->nodes[cidx];
    uint32_t child_min = child->min_width;
    if (child_min == KOS_MATCH_UNBOUNDED)
      child_min = 0;
    if (remaining >= child_min)
      remaining -= child_min;
    else
      remaining = 0;
    uint32_t child_trailing = saturating_add(remaining, trailing_min);
    int r = match_node(e, cidx, cursor, child_trailing);
    if (r != 1)
      return r;
  }
  return 1;
}


// The end.
