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

  int saved_in = e->in_from_datum;
  if (e->subj->kp_cars && *cursor >= 0
      && *cursor < e->subj->cached_length
      && KosDatum_Check(e->subj->kp_cars[*cursor]))
    e->in_from_datum = 1;

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
  if (PyUnicode_Check(peeled) || PyBytes_Check(peeled)
      || PyByteArray_Check(peeled)) {
    e->in_from_datum = saved_in;
    return 0;
  }
  if (! (KosPair_Check(peeled) || KosNilP(peeled)
         || PyList_Check(peeled) || PyTuple_Check(peeled))) {
#ifdef Py_TPFLAGS_SEQUENCE
    /* Sequence-flagged types descend, exactly as CPython's
       MATCH_SEQUENCE does (PEP 634). */
    if (! PyType_HasFeature(Py_TYPE(peeled), Py_TPFLAGS_SEQUENCE)) {
      e->in_from_datum = saved_in;
      return 0;
    }
#else
    /* 3.9: no tp_flags sequence bit; fall back to the ABC check. */
    PyObject *seq_type = get_abc_sequence();
    if (! seq_type) {
      e->in_from_datum = saved_in;
      return -1;
    }
    int isinst = PyObject_IsInstance(peeled, seq_type);
    if (isinst < 0) {
      e->in_from_datum = saved_in;
      return -1;
    }
    if (! isinst) {
      e->in_from_datum = saved_in;
      return 0;
    }
#endif
  }

  KosMatchSubject sub;
  if (subject_from_any(elem, &sub)) {
    /* Not a sequence-like subject — match fails (no exception). */
    PyErr_Clear();
    e->in_from_datum = saved_in;
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
  e->in_from_datum = saved_in;
  KosMatchSubject_Release(&sub);

  if (r != 1)
    return r;

  if (sub_cursor != sub_length)
    return 0;

  (*cursor)++;
  return 1;
}


// The end.
