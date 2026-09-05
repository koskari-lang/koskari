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
  MATCH_CASE: arg = extra index. Schema (LABEL, CONST, CONST, COUNT):
  row.a0 = no-match target PC, row.a1 = frozen matcher 6-tuple cpool
  index, row.a2 = missing-externs name tuple cpool index (the extern
  names not covered by the matcher extern defaults, in extern-table
  order), row.a3 = keep-subject flag. Cache: one KOS_CACHE_MATCHER
  slot holding the Matcher thawed from row.a1 and a KosSlotMap of the
  matcher's capture slots resolved against the namespace layout.

  Stack [..., subject, e0..eN-1] with N = len(row.a2): the compiler
  emits one load per missing extern (VAR, lowered to FAST when a slot
  exists) above the subject, pushed in row field 2 order. The opcode is
  only the interface between the stack, the namespace, and the matcher
  API: the subject is presented as a length-1 spine and the stacked
  segment is handed with its names to KosMatcher_RunSlotsVectorcall,
  which does all extern resolution (inbuilt defaults included), raises
  its own errors, and fills a slot array aligned to the matcher's slot
  table. The stacked externs are consumed on both outcomes, returning
  the subject to TOS. On no match: leave the subject and jump to
  row.a0. On the first match: hand the slot array to
  kos_ns_store_slotmap through the cached KosSlotMap, which binds
  each slot that fired (by fast index, or by name for a slot without
  one) and leaves an unfired slot (an OPTIONAL that did not fire, the
  other branch of a ONEOF) unbound; push nothing. When the
  keep flag is clear the match consumes the subject; when set (a
  guarded arm) the subject stays on the stack, and the guard code is
  responsible for either jumping to row.a0 with the subject intact or
  popping it before the arm body commits.
*/


/**
   The site's KosSlotMap for the matcher's capture slots under ns: the
   cached map when it still holds for the namespace layout, else a
   fresh resolution stored into the cache slot when there is one. New
   reference, or NULL with an exception set. The caller holds this ref
   across the bind, so a re-entry of the site that replaces the cached
   map cannot free the one in use.
*/
static PyObject *match_case_slot_map(
    KosCacheSlot *slot,
    KosMatcher *matcher,
    PyObject *ns) {

  int cached = slot && slot->kind == KOS_CACHE_MATCHER;
  PyObject *map = cached ? slot->u.matcher.slotmap : NULL;
  if (map) {
    int holds = kos_slotmap_holds(map, ns);
    if (holds < 0)
      return NULL;
    if (holds) {
      Py_INCREF(map);
      return map;
    }
  }

  map = KosSlotMap_New(ns, matcher->slots);
  if (! map)
    return NULL;
  if (cached) {
    Py_INCREF(map);
    Py_XSETREF(slot->u.matcher.slotmap, map);
  }
  return map;
}


/**
   Bind the fired slots into ns and consume them. Every entry of
   ``slots`` is released on return, including on error. Returns 0, or
   -1 with an exception set.
*/
static int match_case_bind(
    KosCacheSlot *slot,
    KosMatcher *matcher,
    PyObject *ns,
    PyObject **slots) {

  PyObject *map = match_case_slot_map(slot, matcher, ns);
  if (! map) {
    Py_ssize_t n = KosMatcher_SLOT_COUNT(matcher);
    for (Py_ssize_t i = 0; i < n; i++)
      Py_CLEAR(slots[i]);
    return -1;
  }

  int rc = kos_ns_store_slotmap(ns, map, slots);
  Py_DECREF(map);
  return rc;
}


KOS_OP_HANDLER_COLD(MATCH_CASE) {
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *ns = CURRENT_NS();
  KOS_OP_REQUIRE_NS(ns);

  /* Thaw the matcher once into this site's cache slot. A populated slot is
     permanently valid: the thaw is a pure function of the const. When the
     cache is gated off (no slot) we thaw locally and own the ref. */
  PyObject *matcher = NULL;
  int matcher_owned = 0;
  KosCacheSlot *slot = KosOp_SLOT(insn);
  if (slot && slot->kind == KOS_CACHE_MATCHER)
    matcher = slot->u.matcher.matcher;   /* borrowed */
  if (! matcher) {
    PyObject *frozen = KosExtra_OBJ(row, 1);
    matcher = KosMatcher_FromTuple(frozen);
    if (unlikely(! matcher))
      RESULT_RAISE();
    if (slot) {
      slot->kind = KOS_CACHE_MATCHER;
      slot->u.matcher.matcher = matcher;   /* slot takes ownership */
      slot->u.matcher.slotmap = NULL;
    } else {
      matcher_owned = 1;
    }
  }

  /* The stacked missing externs sit above the subject; tail[0] is the
     subject, tail[1..n] the values named by row.a2. The subject
     becomes a length-1 spine directly; no wrapper tuple is
     materialized. */
  PyObject *names = KosExtra_OBJ(row, 2);
  Py_ssize_t n = 0;
  if (names && PyTuple_Check(names))
    n = PyTuple_GET_SIZE(names);

  PyObject *const *tail =
      KosEvalState_StackTailPtr(es, (size_t) (n + 1));
  if (unlikely(! tail)) {
    PyErr_SetString(PyExc_SystemError,
                    "MATCH_CASE stack underflow");
    if (matcher_owned)
      Py_DECREF(matcher);
    RESULT_RAISE();
  }

  /* The capture slot array: inline for the common small table. */
  Py_ssize_t n_slots = KosMatcher_SLOT_COUNT(matcher);
  PyObject *inline_slots[KOS_MATCH_SLOTS_INLINE];
  PyObject **slots = inline_slots;
  if (n_slots > KOS_MATCH_SLOTS_INLINE) {
    slots = PyMem_Malloc((size_t) n_slots * sizeof(PyObject *));
    if (unlikely(! slots)) {
      PyErr_NoMemory();
      if (matcher_owned)
        Py_DECREF(matcher);
      RESULT_RAISE();
    }
  }

  KosMatchSubject subj;
  KosMatchSubject_FromSingle(tail[0], &subj);

  int matched = KosMatcher_RunSlotsVectorcall(
      (KosMatcher *) matcher, &subj,
      n ? tail + 1 : NULL, names, slots);
  KosMatchSubject_Release(&subj);

  int rc = 0;
  if (matched > 0) {
    /* Consume the stacked externs, and the subject unless kept, before
       binding so a raise leaves a consistent stack. */
    if (n)
      KosEvalState_StackDropNoErr(es, (size_t) n);
    if (! KosExtra_IMM(row, 3))
      KosEvalState_StackDropNoErr(es, 1);
    rc = match_case_bind(slot, (KosMatcher *) matcher, ns, slots);
  } else if (matched == 0 && n) {
    KosEvalState_StackDropNoErr(es, (size_t) n);
  }

  if (slots != inline_slots)
    PyMem_Free(slots);
  if (matcher_owned)
    Py_DECREF(matcher);

  if (unlikely(matched < 0 || rc < 0))
    RESULT_RAISE();

  if (! matched) {
    RESULT_JUMP_AT(es, KosExtra_TARGET(row, 0));
  }

  RESULT_NEXT(es);
}


// The end.
