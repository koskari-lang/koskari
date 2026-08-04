/* Included by matcher.c — match_match */
/* === MATCH matching === */


static void width_match(KosMatcher *m, KosMatchNode *n) {
  chain_width(m, n->as.match.first_child, &n->min_width, &n->max_width);
}


static int match_match(
    MatchEngine *e,
    const KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  /* Sum of min_widths of all children — we drain it as we iterate to
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
