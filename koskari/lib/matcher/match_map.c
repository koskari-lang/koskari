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
  Included by matcher.c. The MAP and MAP_ENTRY match steps.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === MAP / MAP_ENTRY matching === */


/**
   Match one MAP entry against a mapping subject: resolve the key
   (const or extern), look it up, and match the entry's sub-matcher
   against the looked-up value in its own synthesized single-element
   subject. A missing key folds to a soft no-match; other lookup
   errors propagate.
*/
/* Probe a mapping for key without changing it. An exact dict is read
   directly; any other mapping answers through its own get() with a
   private default, as the Python match statement does, so a
   defaultdict or another __missing__ mapping neither inserts nor
   invents the key. New reference; NULL with no error on a miss, NULL
   with an error on failure. */
static PyObject *map_probe(PyObject *mapping, PyObject *key) {
  if (PyDict_CheckExact(mapping)) {
    PyObject *val = PyDict_GetItemWithError(mapping, key);
    Py_XINCREF(val);
    return val;
  }

  static PyObject *absent = NULL;
  if (! absent) {
    absent = PyObject_CallNoArgs((PyObject *) &PyBaseObject_Type);
    if (! absent)
      return NULL;
  }

  PyObject *val = PyObject_CallMethod(mapping, "get", "OO", key, absent);
  if (val == absent) {
    Py_DECREF(val);
    return NULL;
  }
  return val;
}


static int match_one_mentry(
    MatchEngine *e,
    KosMatchNode *en,
    PyObject *mapping) {

  PyObject *key;
  if (en->as.map_entry.key_kind == KOS_MATCH_MAPKEY_CONST) {
    key = PyTuple_GET_ITEM(e->m->consts, en->as.map_entry.key_ref);
  } else {
    if (! e->extern_env) {
      PyErr_SetString(PyExc_RuntimeError,
                      "MAP extern key requires an extern environment");
      return -1;
    }
    key = e->extern_env[en->as.map_entry.key_ref];
  }

  PyObject *val = map_probe(mapping, key);
  if (! val)
    return PyErr_Occurred()? -1: 0;

  int r = match_projected_value(e, en->as.map_entry.subnode, val);
  Py_DECREF(val);
  return r;
}


/**
   MAP: match the current spine element as a mapping. The element must
   be dict-like (PyDict, or an instance of collections.abc.Mapping);
   each entry's key must be present with a value matching the entry's
   sub-pattern. Extra keys are ignored, per PEP 634 mapping-pattern
   semantics.
*/
static int match_map(
    MatchEngine *e,
    KosMatchNode *n,
    Py_ssize_t *cursor,
    uint32_t trailing_min) {

  Py_ssize_t length = e->subj->vt->length(e->subj);
  if (*cursor + 1 + (Py_ssize_t) trailing_min > length)
    return 0;

  PyObject *elem = e->subj->vt->get(e->subj, *cursor);
  PyObject *mapping = matcher_peel_datum(elem);

  {
    int gate = matcher_mapping_gate(mapping);
    if (gate <= 0)
      return gate;
  }

  for (uint32_t eidx = n->as.map.first_entry;
       eidx != KOS_MATCH_NO_INDEX;
       eidx = e->m->nodes[eidx].next) {
    KosMatchNode *en = &e->m->nodes[eidx];
    int r = match_one_mentry(e, en, mapping);
    if (r != 1)
      return r;
  }

  (*cursor)++;
  return 1;
}


// The end.
