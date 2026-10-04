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
  Included by matcher.c. The FORM match step.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

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

  /* PEP 634 sequence-pattern gate. str / bytes / bytearray are atoms,
     since destructuring a string into characters is never what a match
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

  /* The gate accepted the element, so a failed build is a real error
     from reading a Python sequence or walking a looping spine, and it
     propagates. An improper pair spine is a shape: no match. */
  KosMatchSubject sub;
  if (KosPair_Check(peeled) || KosNilP(peeled)) {
    int r = KosMatchSubject_FromKosPair(peeled, &sub);

    if (r)
      return r > 0 ? 0 : -1;
  } else if (KosMatchSubject_FromPySequence(peeled, &sub) < 0) {
    return -1;
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
