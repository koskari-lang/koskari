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
  Included by matcher.c. The GROUP match step and its width.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === GROUP matching === */


static void width_group(KosMatcher *m, KosMatchNode *n) {
  chain_width(m, n->as.group.first_child, &n->min_width, &n->max_width);
}


/**
   Run GROUP children in an isolated capture dict. On success *out_sub
   receives a new reference to the populated dict (caller must DECREF).
*/
static int match_group(
    MatchEngine *e,
    const KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min,
    PyObject **out_sub) {

  uint32_t remaining = 0;
  for (uint32_t cidx = n->as.group.first_child;
       cidx != KOS_MATCH_NO_INDEX;
       cidx = e->m->nodes[cidx].next) {
    uint32_t cm = e->m->nodes[cidx].min_width;
    if (cm != KOS_MATCH_UNBOUNDED)
      remaining = saturating_add(remaining, cm);
  }

  Py_ssize_t snap = engine_snapshot(e);
  PyObject *parent_dict = e->cur_captures;
  PyObject *old_capture_parent = e->capture_parent;
  Py_ssize_t start = *cursor;
  PyObject *sub = match_frame_new(e->subj);
  if (! sub)
    return -1;

  e->capture_parent = e->cur_captures;
  e->cur_captures = sub;

  for (uint32_t cidx = n->as.group.first_child;
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
    if (r != 1) {
      e->cur_captures = parent_dict;
      e->capture_parent = old_capture_parent;
      engine_rollback(e, snap);
      Py_DECREF(sub);
      return r;
    }
  }

  e->cur_captures = parent_dict;
  e->capture_parent = old_capture_parent;
  if (datum_frame_set_source(sub, e->subj, start, *cursor)) {
    Py_DECREF(sub);
    return -1;
  }

  /* The frame is complete: its records would let a rollback past this
     point empty it while a harvested row still holds it. Backtracking
     drops the frame whole through the record the caller adds. */
  engine_discard_pending_change_refs(e, snap);
  *out_sub = sub;
  return 1;
}


// The end.
