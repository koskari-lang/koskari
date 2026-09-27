/* Included by matcher.c — match_form */
/* === FORM matching === */


static int match_form(
    MatchEngine *e,
    const KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t length = e->subj->vt->length(e->subj);
  if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
    return 0;

  PyObject *elem = e->subj->vt->get(e->subj, *cursor);

  /* PEP 634 sequence-pattern gate. str / bytes / bytearray are atoms —
     destructuring a string into characters is never what a match
     meant. Beyond the KosPair spine and the list/tuple fast paths,
     only sequence types descend (Py_TPFLAGS_SEQUENCE on 3.10+, the
     collections.abc.Sequence ABC on 3.9): mappings and sets are not
     sequences (PySequence_Fast would happily iterate a dict's keys),
     and an iterator must never be silently consumed by a failed arm.
     Soft no-match for all of them. */
  PyObject *peeled = matcher_peel_datum(elem);
  {
    int gate = matcher_sequence_gate(peeled);
    if (gate <= 0)
      return gate;
  }

  KosMatchSubject sub;
  if (subject_from_any(elem, &sub)) {
    /* Not a sequence-like subject — match fails (no exception). */
    PyErr_Clear();
    return 0;
  }

  /* Save engine subject and replace with sub-subject for the inner
     branch. */
  KosMatchSubject *saved = e->subj;
  e->subj = &sub;

  Py_ssize_t sub_cursor = 0;
  int r = match_node(e, n->as.form.subnode, &sub_cursor, 0);

  Py_ssize_t sub_length = sub.vt->length(&sub);
  e->subj = saved;
  KosMatchSubject_Release(&sub);

  if (r != 1)
    return r;

  if (sub_cursor != sub_length)
    return 0;

  (*cursor)++;
  return 1;
}


// The end.
