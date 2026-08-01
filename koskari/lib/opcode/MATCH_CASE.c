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
  MATCH_CASE: arg = extra index. Schema (LABEL, CONST, CONST, NOARG):
  row.a0 = no-match target PC, row.a1 = frozen matcher 6-tuple cpool
  index, row.a2 = externs name tuple cpool index. Cache: one
  KOS_CACHE_MATCHER slot holding the Matcher thawed from row.a1.

  Stack [..., subject] with subject = TOS. The subject is wrapped as a
  length-1 sequence so the whole value matches as one spine element, then
  the arm matcher runs against it with an extern env resolved from the
  local namespace. On no match: leave the subject and jump to row.a0. On
  the first match: drop the subject and push the capture dict, falling
  through to the arm's MATCH_STORE_NAMES (or a POP for a capture-less arm)
  and its body.
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

  /* Wrap the subject (borrowed TOS) as a length-1 sequence. */
  PyObject *subject = KosEvalState_StackPeek(es);
  if (unlikely(! subject)) {
    if (matcher_owned)
      Py_DECREF(matcher);
    return EVAL_RAISE();
  }
  PyObject *one = PyTuple_Pack(1, subject);
  if (unlikely(! one)) {
    if (matcher_owned)
      Py_DECREF(matcher);
    return EVAL_RAISE();
  }

  /* Resolve the extern env aligned to the matcher's extern table, if any. */
  PyObject *extern_names = KosEvalState_CONST(es, row.a2);
  PyObject *extern_env = NULL;
  if (extern_names && PyTuple_Check(extern_names)
      && PyTuple_GET_SIZE(extern_names) > 0) {
    Py_ssize_t n = PyTuple_GET_SIZE(extern_names);
    extern_env = PyTuple_New(n);
    if (unlikely(! extern_env)) {
      Py_DECREF(one);
      if (matcher_owned)
        Py_DECREF(matcher);
      return EVAL_RAISE();
    }
    for (Py_ssize_t i = 0; i < n; i++) {
      PyObject *name = PyTuple_GET_ITEM(extern_names, i);
      PyObject *val = frame_ns_deref(es, name);
      if (unlikely(! val)) {
        Py_DECREF(extern_env);
        Py_DECREF(one);
        if (matcher_owned)
          Py_DECREF(matcher);
        return EVAL_RAISE();
      }
      PyTuple_SET_ITEM(extern_env, i, val);   /* steals val */
    }
  }

  KosMatchSubject subj;
  if (unlikely(KosMatchSubject_FromPySequence(one, &subj))) {
    Py_XDECREF(extern_env);
    Py_DECREF(one);
    if (matcher_owned)
      Py_DECREF(matcher);
    return EVAL_RAISE();
  }

  PyObject *result = KosMatcher_Match((KosMatcher *) matcher, &subj,
                                      extern_env);
  KosMatchSubject_Release(&subj);
  Py_XDECREF(extern_env);
  Py_DECREF(one);
  if (matcher_owned)
    Py_DECREF(matcher);

  if (unlikely(! result))
    return EVAL_RAISE();

  if (! ((KosMatchResult *) result)->matched) {
    Py_DECREF(result);
    KosEvalState_SET_PC(es, row.a0);
    return EVAL_NORMAL();
  }

  /* Committed: drop the subject, push the capture dict for the arm's
     MATCH_STORE_NAMES (or a following POP for a capture-less arm). */
  PyObject *caps = ((KosMatchResult *) result)->captures;
  Py_INCREF(caps);
  Py_DECREF(result);
  KosEvalState_StackDropNoErr(es, 1);
  return kos_op_result_PUSH(es, caps);
}


// The end.
