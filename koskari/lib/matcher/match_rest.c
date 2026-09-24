/* Included by matcher.c — match_rest */
/* === REST matching === */


/**
   Build the REST capture value for spine range [start, end) after a
   successful REST match (consume == end - start). An empty REST binds
   the subject's empty remainder, the terminator cell for a pair spine
   and () for a sequence, whatever follows it on the spine.
*/
static PyObject *
rest_value_for_range(
    MatchEngine *e,
    Py_ssize_t start,
    Py_ssize_t end,
    Py_ssize_t consume,
    Py_ssize_t length) {

  if (consume == 0) {
    PyObject *value = e->subj->vt->tail(e->subj, length);
    if (! value)
      return NULL;
    return kos_datum_from_spine_cell(e->subj, length, value);
  }
  if (end == length) {
    PyObject *value = e->subj->vt->tail(e->subj, start);
    if (! value)
      return NULL;
    return kos_datum_from_spine_cell(e->subj, start, value);
  }
  return wrap_slice_datums(e->subj, start, end);
}


static int match_rest(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t length = e->subj->vt->length(e->subj);
  Py_ssize_t available = length - *cursor;
  if (available < (Py_ssize_t) trailing_min)
    return 0;

  Py_ssize_t consume = available - (Py_ssize_t) trailing_min;
  Py_ssize_t end = *cursor + consume;
  uint32_t min_tail = n->as.rest.min_tail;

  if (consume < (Py_ssize_t) min_tail)
    return 0;

  *cursor = end;
  return 1;
}


// The end.
