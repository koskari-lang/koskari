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
  slot holding the Matcher thawed from row.a1.

  Stack [..., subject, e0..eN-1] with N = len(row.a2): the compiler
  emits one load per missing extern (VAR, lowered to FAST when a slot
  exists) above the subject, pushed in row.a2 order. The opcode is
  only the interface between the stack and the matcher API: the
  subject is presented as a length-1 spine and the stacked segment is
  handed with its names to KosMatcher_RunVectorcall, which does all
  extern resolution (inbuilt defaults included) and raises its own
  errors. The stacked externs are consumed on both outcomes,
  returning the subject to TOS. On no match: leave the subject and
  jump to row.a0. On the first match: push the capture dict for the
  arm's MATCH_STORE_NAMES (or a following POP for a capture-less
  arm). When the keep flag is clear the match consumes the subject;
  when set (a guarded arm) the subject stays beneath the capture
  dict, and the guard code is responsible for either jumping to
  row.a0 with the subject intact or popping it before the arm body
  commits.
*/

KOS_OP_HANDLER_COLD(MATCH_CASE) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  /* Thaw the matcher once into this site's cache slot. A populated slot is
     permanently valid: the thaw is a pure function of the const. When the
     cache is gated off (no slot) we thaw locally and own the ref. */
  PyObject *matcher = NULL;
  int matcher_owned = 0;
  uint16_t cache_ix = KosOp_CACHE(insn);
  KosCacheSlot *slot = NULL;
  if (cache_ix != KOS_CACHE_UNSET) {
    slot = &KosCode_CACHE(code)[cache_ix];
    if (slot->kind == KOS_CACHE_MATCHER)
      matcher = slot->u.matcher.matcher;   /* borrowed */
  }
  if (! matcher) {
    PyObject *frozen = KosEvalState_CONST(es, row.a1);
    if (unlikely(! frozen))
      return EVAL_RAISE();
    matcher = KosMatcher_FromTuple(frozen);
    if (unlikely(! matcher))
      return EVAL_RAISE();
    if (slot) {
      slot->kind = KOS_CACHE_MATCHER;
      slot->u.matcher.matcher = matcher;   /* slot takes ownership */
    } else {
      matcher_owned = 1;
    }
  }

  /* The stacked missing externs sit above the subject; tail[0] is the
     subject, tail[1..n] the values named by row.a2. The subject
     becomes a length-1 spine directly; no wrapper tuple is
     materialized. */
  PyObject *names = KosEvalState_CONST(es, row.a2);
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
    return EVAL_RAISE();
  }

  KosMatchSubject subj;
  KosMatchSubject_FromSingle(tail[0], &subj);

  PyObject *caps = NULL;
  int matched = KosMatcher_RunVectorcall(
      (KosMatcher *) matcher, &subj,
      n ? tail + 1 : NULL, names, &caps);
  KosMatchSubject_Release(&subj);
  if (matcher_owned)
    Py_DECREF(matcher);

  if (unlikely(matched < 0))
    return EVAL_RAISE();

  /* Consume the stacked externs; the subject returns to TOS. */
  if (n)
    KosEvalState_StackDropNoErr(es, (size_t) n);

  if (! matched) {
    KosEvalState_SET_PC(es, KosEvalState_JumpTarget(es, row.a0));
    return EVAL_NORMAL();
  }

  /* Matched: push the capture dict for the arm's MATCH_STORE_NAMES (or
     a following POP for a capture-less arm). Without the keep flag the
     subject is consumed here; with it (a guarded arm) the subject stays
     beneath the dict for the guard's advance-or-commit. */
  if (! row.a3)
    KosEvalState_StackDropNoErr(es, 1);
  return kos_op_result_PUSH(es, caps);
}


// The end.
