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
  koskari/lib/eval/eval_match.c

  The bodies of the MATCH_* instructions. An instruction with a slot
  field has VAR and FAST twins that tail-call one body, and the body
  reads which it is off the opcode to resolve its slot operands by
  name or by index; an instruction with no slot field has one form.
  Included into eval.c ahead of the opcode handlers. A walk's
  cursors, marks and items lists are stack entries named by depth
  (design/interpreter/matcher-core.md). The steps themselves are the
  matcher subsystem's (matcher.h, matcher/step.c, matcher/cursor.c,
  matcher/mark.c); a body is the interface between the row and the
  stack.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/


/* === slot operands === */


/**
   The value of a slot operand: by fast index on a FAST twin, by name
   on a VAR twin. New reference, or NULL with an exception set.
*/
KOS_OP_INLINE PyObject *kos_match_operand(
    KosEvalState *es,
    const KosExtra *row,
    int field,
    int fast) {

  if (fast)
    return kos_ns_fast_deref(
        KosEvalState_NAMESPACE(es), (size_t) KosExtra_IMM(row, field));
  return frame_ns_deref(es, KosExtra_OBJ(row, field));
}


/**
   cls.__match_args__ for an instruction whose subject has passed cls's
   type test, from its cache slot when the slot holds cls at its current
   version; otherwise the slot is filled from KosMatch_PlainMatchArgs
   when that decides the tuple. Borrowed, or NULL for the step to read
   it from cls; never an exception.
*/
KOS_OP_INLINE PyObject *kos_match_args_cached(void *insn, PyObject *cls) {
  KosCacheSlot *slot = KosOp_SLOT(insn);

  if (unlikely(! slot))
    return NULL;
  if (likely(slot->kind == KOS_CACHE_MATCH_ARGS
             && slot->u.match_args.cls == cls
             && slot->u.match_args.type_version
                == kos_type_version((PyTypeObject *) cls)))
    return slot->u.match_args.match_args;

  PyObject *match_args = KosMatch_PlainMatchArgs(cls);
  if (! match_args)
    return NULL;
  uint64_t version = kos_type_version_assign((PyTypeObject *) cls);
  if (version && (slot->kind == KOS_CACHE_EMPTY
                  || slot->kind == KOS_CACHE_MATCH_ARGS)) {
    slot->kind = KOS_CACHE_MATCH_ARGS;
    slot->u.match_args.cls = cls;
    slot->u.match_args.type_version = version;
    slot->u.match_args.match_args = match_args;
  }
  return match_args;
}


/**
   The element a peek instruction tests: top of stack, borrowed. NULL
   with an exception set on an empty stack.
*/
KOS_OP_INLINE PyObject *kos_match_peek(KosEvalState *es) {
  PyObject *elem = KosEvalState_StackPeek(es);

  if (unlikely(! elem))
    PyErr_SetString(PyExc_SystemError, "match instruction on an empty stack");
  return elem;
}


#define KOS_MATCH_BODY(name)                                            \
  static KOS_PRESERVE_NONE KosOpResult name(                            \
      KosEvalState *es, void *insn, PyObject *kos_match_delivery_)

#define KOS_MATCH_MISS(row)  RESULT_JUMP_AT(es, KosExtra_TARGET((row), 0))


/**
   The list an append instruction names by its depth under the top,
   owned by its entry alone: the entry's list when nothing else holds
   it, else a copy stored at the entry, so a cloned segment keeps the
   items it had when the clone was taken. Borrowed; NULL with an
   exception set on an underrun, a non-list, or a failed copy.
*/
KOS_OP_INLINE PyObject *kos_match_list_at(KosEvalState *es, size_t under) {
  PyObject *list = KosEvalState_StackPeekUnder(es, under);

  if (unlikely(! list))
    return NULL;
  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "match append requires a list, not %.200s",
                 Py_TYPE(list)->tp_name);
    return NULL;
  }
  if (Py_REFCNT(list) == 1)
    return list;

  PyObject *copy = PyList_GetSlice(list, 0, PyList_GET_SIZE(list));
  if (unlikely(! copy))
    return NULL;
  if (unlikely(KosEvalState_StackSetUnder(es, under, copy) < 0))
    return NULL;
  return copy;
}


/**
   Store an owned value into the entry a RAW field names, releasing
   what it held; consumed on every path. 0, or -1 with an exception
   set.
*/
KOS_OP_INLINE int kos_match_set_at(
    KosEvalState *es,
    const KosExtra *row,
    int field,
    PyObject *val) {

  return KosEvalState_StackSetUnder(
      es, (size_t) KosExtra_IMM(row, field), val);
}


/* === entries by depth === */


/**
   The cursor at the depth under the top, as the instruction found the
   stack. Borrowed; NULL with an exception set on an underrun,
   SystemError when the entry is not a cursor.
*/
KOS_OP_INLINE PyObject *kos_match_cursor_at(
    KosEvalState *es,
    uint32_t under) {

  PyObject *entry = KosEvalState_StackPeekUnder(es, (size_t) under);

  if (unlikely(entry && ! KosMatchCursor_Check(entry))) {
    PyErr_Format(PyExc_SystemError,
                 "match instruction finds %.200s at depth %u, not a cursor",
                 Py_TYPE(entry)->tp_name, (unsigned) under);
    return NULL;
  }
  return entry;
}


/**
   The mark at the depth under the top, as the instruction found the
   stack. Borrowed; NULL with an exception set on an underrun,
   SystemError when the entry is not a mark.
*/
KOS_OP_INLINE PyObject *kos_match_mark_at(
    KosEvalState *es,
    uint32_t under) {

  PyObject *entry = KosEvalState_StackPeekUnder(es, (size_t) under);

  if (unlikely(entry && ! KosMatchMark_Check(entry))) {
    PyErr_Format(PyExc_SystemError,
                 "match instruction finds %.200s at depth %u, not a mark",
                 Py_TYPE(entry)->tp_name, (unsigned) under);
    return NULL;
  }
  return entry;
}


/**
   Slot i of the mark at the depth under the top: *out the bound value
   (borrowed), or NULL for an empty slot. 0, or -1 with an exception
   set, SystemError for an index outside the mark's slot count.
*/
KOS_OP_INLINE int kos_match_slot_at(
    KosEvalState *es,
    uint32_t under,
    uint32_t i,
    PyObject **out) {

  PyObject *mark = kos_match_mark_at(es, under);

  if (unlikely(! mark))
    return -1;
  if (unlikely((Py_ssize_t) i >= KosMatchMark_COUNT(mark))) {
    PyErr_Format(PyExc_SystemError,
                 "match slot %u outside a mark of %zd slots",
                 (unsigned) i, KosMatchMark_COUNT(mark));
    return -1;
  }
  *out = KosMatchMark_SLOT(mark, i);
  return 0;
}


/**
   The mark at the depth a row's field names, made writable: the mark
   itself when its entry alone holds it, else a copy stored at the
   entry. Borrowed; NULL with an exception set.
*/
KOS_OP_INLINE PyObject *kos_match_writable_at(
    KosEvalState *es,
    const KosExtra *row,
    int field) {

  PyObject *mark = kos_match_mark_at(es, KosExtra_IMM(row, field));

  if (unlikely(! mark))
    return NULL;

  PyObject *target = KosMatchMark_Writable(mark, 1);
  if (unlikely(! target))
    return NULL;
  if (unlikely(target != mark)
      && unlikely(kos_match_set_at(es, row, field, target) < 0))
    return NULL;
  return target;
}


/* === cursors === */


/**
   MATCH_DESCEND: the sequence gate on TOS read through Datum; the
   cursor over it replaces it. A miss pops TOS and jumps. Simple LABEL
   style.
*/
KOS_MATCH_BODY(kos_match_descend) {
  KOS_SYNC_PC(es, insn);
  (void) kos_match_delivery_;

  PyObject *value = kos_match_peek(es);
  if (unlikely(! value))
    RESULT_RAISE();

  PyObject *cursor = NULL;
  int r = KosMatchCursor_Descend(value, &cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();

  if (! r) {
    if (unlikely(KosEvalState_StackDrop1(es) < 0))
      RESULT_RAISE();
    RESULT_JUMP_AT(es, KosOp_TARGET(insn));
  }
  if (unlikely(KosEvalState_StackSetUnder(es, 0, cursor) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_SINGLE: a one-element cursor over TOS replaces it. No arg.
*/
KOS_MATCH_BODY(kos_match_single) {
  KOS_SYNC_PC(es, insn);
  (void) kos_match_delivery_;

  PyObject *value = kos_match_peek(es);
  if (unlikely(! value))
    RESULT_RAISE();

  PyObject *cursor = KosMatchCursor_Single(value);
  if (unlikely(! cursor))
    RESULT_RAISE();
  if (unlikely(KosEvalState_StackSetUnder(es, 0, cursor) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_FITS: the cursor at field 1 must have at least field 2 and
   at most field 3 elements left, KOS_MATCH_UNBOUNDED for no maximum.
   Row LABEL, depth, min, max.
*/
KOS_MATCH_BODY(kos_match_fits) {
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE_AT(es, insn);

  Py_ssize_t remaining = KosMatchCursor_Remaining(cursor);
  uint32_t max = KosExtra_IMM(row, 3);
  if (remaining < (Py_ssize_t) KosExtra_IMM(row, 2)
      || (max != KOS_MATCH_UNBOUNDED && remaining > (Py_ssize_t) max))
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT: the next element of the cursor at field 1 pushed, the
   cursor moved in place or its copy stored back; a miss at the end.
   Row LABEL, depth.
*/
/**
   The next element of the cursor at depth under, the cursor moved in
   place or its copy stored back at under: 1 with *out the element, a
   new reference; 0 at the end; -1 with an exception set. The pull of
   MATCH_NEXT and MATCH_SCOPE_NEXT.
*/
KOS_OP_INLINE int kos_match_pull(
    KosEvalState *es,
    PyObject *cursor,
    size_t under,
    PyObject **out) {

  PyObject *next = NULL;
  int r = KosMatchCursor_Next(cursor, 1, out, &next);
  if (r <= 0)
    return r;

  if (unlikely(next != cursor)
      && unlikely(KosEvalState_StackSetUnder(es, under, next) < 0)) {
    Py_CLEAR(*out);
    return -1;
  }
  return 1;
}


KOS_MATCH_BODY(kos_match_next) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  uint32_t under = KosExtra_IMM(row, 1);
  PyObject *cursor = kos_match_cursor_at(es, under);
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *elem = NULL;
  int r = kos_match_pull(es, cursor, (size_t) under, &elem);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, elem);
}


/**
   MATCH_BOUND: a cursor at the position of the cursor at field 1,
   ending field 2 elements before its end, pushed; a miss when fewer
   remain. Row LABEL, depth, width.
*/
KOS_MATCH_BODY(kos_match_bound) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *bound = NULL;
  int r = KosMatchCursor_Bound(
      cursor, (Py_ssize_t) KosExtra_IMM(row, 2), &bound);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, bound);
}


/**
   MATCH_UNBOUND: a cursor at the position of the cursor at field 0
   with no reserve, ending at its subject's end, pushed. Row depth.
*/
KOS_MATCH_BODY(kos_match_unbound) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  RESULT_STEAL(es, KosMatchCursor_Unbound(cursor));
}


/**
   MATCH_SYNC: the cursor at field 0 moved to the position of the
   cursor at field 1. Row depth, depth.
*/
KOS_MATCH_BODY(kos_match_sync) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *to = kos_match_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! to))
    RESULT_RAISE();

  PyObject *moved = KosMatchCursor_Sync(cursor, to, 1);
  if (unlikely(! moved))
    RESULT_RAISE();
  if (unlikely(moved != cursor)
      && unlikely(kos_match_set_at(es, row, 0, moved) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_TAIL: the remaining elements of the cursor at field 1 less
   field 2 pushed as a span, the cursor advanced past them; a miss
   when fewer than field 2 remain or the span is shorter than field 3.
   Row LABEL, depth, width, min.
*/
KOS_MATCH_BODY(kos_match_tail) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *span = NULL;
  PyObject *next = NULL;
  int r = KosMatchCursor_Tail(
      cursor, (Py_ssize_t) KosExtra_IMM(row, 2),
      (Py_ssize_t) KosExtra_IMM(row, 3), 1, &span, &next);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (unlikely(next != cursor)
      && unlikely(kos_match_set_at(es, row, 1, next) < 0)) {
    Py_DECREF(span);
    RESULT_RAISE();
  }
  RESULT_STEAL(es, span);
}


/* === marks === */


/**
   MATCH_SCOPE: a mark at the position of the cursor at field 0 with
   field 1 empty slots pushed. Row depth, count.
*/
KOS_MATCH_BODY(kos_match_scope) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  RESULT_STEAL(
      es, KosMatchMark_New(cursor, (Py_ssize_t) KosExtra_IMM(row, 1)));
}


/**
   MATCH_SCOPE_NEXT: MATCH_SCOPE on the cursor at field 1 with field 2
   empty slots, then MATCH_NEXT on that cursor, one entry deeper under
   the mark; a miss at the end leaves the mark for the fail entry the
   NEXT's miss label names. Row LABEL, depth, count.
*/
KOS_MATCH_BODY(kos_match_scope_next) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  uint32_t under = KosExtra_IMM(row, 1);
  PyObject *cursor = kos_match_cursor_at(es, under);
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *mark = KosMatchMark_New(
      cursor, (Py_ssize_t) KosExtra_IMM(row, 2));
  if (unlikely(! mark))
    RESULT_RAISE();
  if (unlikely(KosEvalState_StackPUSH(es, mark) < 0))
    RESULT_RAISE();

  PyObject *elem = NULL;
  int r = kos_match_pull(es, cursor, (size_t) under + 1, &elem);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, elem);
}


/**
   MATCH_RESET: the cursor at field 0 moved back to the position of
   the mark at field 1. Row depth, depth.
*/
KOS_MATCH_BODY(kos_match_reset) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *mark = kos_match_mark_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! mark))
    RESULT_RAISE();

  PyObject *moved = KosMatchMark_Reset(cursor, mark, 1);
  if (unlikely(! moved))
    RESULT_RAISE();
  if (unlikely(moved != cursor)
      && unlikely(kos_match_set_at(es, row, 0, moved) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_AT: the element at the position of the mark at field 0
   pushed. Row depth.
*/
KOS_MATCH_BODY(kos_match_at) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *mark = kos_match_mark_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! mark))
    RESULT_RAISE();
  RESULT_STEAL(es, KosMatchMark_At(mark));
}


/**
   MATCH_SPAN: the span from the position of the mark at field 1 to
   the cursor at field 2 pushed; an empty span jumps to field 0,
   pushing nothing. Row LABEL, depth, depth.
*/
KOS_MATCH_BODY(kos_match_span) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *mark = kos_match_mark_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! mark))
    RESULT_RAISE();
  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 2));
  if (unlikely(! cursor))
    RESULT_RAISE();

  if (KosMatchCursor_INDEX(cursor) == KosMatchMark_INDEX(mark))
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, KosMatchMark_Span(mark, cursor));
}


/**
   MATCH_STORE: TOS popped into slot field 1 of the mark at field 0;
   when the cursor at field 2 walks a pair spine, a Datum value takes
   the slot's name from the names tuple in field 3 as its binding.
   Field 2 is 0 for a value no cursor produced, depth 0 being the value
   itself. Row depth, index, depth, const.
*/
KOS_MATCH_BODY(kos_match_store) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  Py_ssize_t i = (Py_ssize_t) KosExtra_IMM(row, 1);
  uint32_t under = KosExtra_IMM(row, 2);
  (void) kos_match_delivery_;

  /* the code's validation holds the index within the names */
  PyObject *name = NULL;
  if (under) {
    PyObject *cursor = kos_match_cursor_at(es, under);
    if (unlikely(! cursor))
      RESULT_RAISE();
    if (KosMatchCursor_KIND(cursor) == KOS_MATCH_SPINE)
      name = PyTuple_GET_ITEM(KosExtra_OBJ(row, 3), i);
  }

  PyObject *mark = kos_match_writable_at(es, row, 0);
  if (unlikely(! mark))
    RESULT_RAISE();

  PyObject *value = KosEvalState_StackPopRef(es);
  if (unlikely(! value))
    RESULT_RAISE();
  if (unlikely(KosMatchMark_Store(mark, i, value, name) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_CLEAR: the slots the index tuple in field 1 names emptied in
   the mark at field 0. Row depth, const.
*/
KOS_MATCH_BODY(kos_match_clear) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *mark = kos_match_writable_at(es, row, 0);
  if (unlikely(! mark))
    RESULT_RAISE();
  if (unlikely(KosMatchMark_Clear(mark, KosExtra_OBJ(row, 1)) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_CAPTURES and MATCH_PASS: the captures of the mark at field
   0, keyed by the names tuple in field 2, over the span to the cursor
   at field 1, pushed; PASS pushes a REPEAT pass's value instead, the
   captures in their MATCH_CAPTURES form when field 3 is set. Row
   depth, depth, const, flag.
*/
KOS_MATCH_BODY(kos_match_captures) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int pass = KosOp_OP(insn) == KOS_OP_MATCH_PASS;
  (void) kos_match_delivery_;

  PyObject *mark = kos_match_mark_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! mark))
    RESULT_RAISE();
  PyObject *cursor = kos_match_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *names = KosExtra_OBJ(row, 2);
  RESULT_STEAL(es, pass
               ? KosMatchMark_Pass(
                   mark, cursor, names, KosExtra_IMM(row, 3) != 0)
               : KosMatchMark_Captures(mark, cursor, names));
}


/**
   MATCH_LOAD: slot field 2 of the mark at field 1 pushed; an empty
   slot jumps to field 0, pushing nothing. Row LABEL, depth, index.
*/
KOS_MATCH_BODY(kos_match_load) {
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *value = NULL;
  if (unlikely(kos_match_slot_at(
          es, KosExtra_IMM(row, 1), KosExtra_IMM(row, 2), &value) < 0))
    RESULT_RAISE_AT(es, insn);
  if (! value)
    KOS_MATCH_MISS(row);
  RESULT_PUSH(es, value);
}


/* === items === */


/**
   MATCH_APPEND: TOS popped and appended to the items list at field
   0, which its entry alone holds once kos_match_list_at has copied a
   shared one. Row depth.
*/
KOS_MATCH_BODY(kos_match_append) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *list = kos_match_list_at(es, (size_t) KosExtra_IMM(row, 0));
  if (unlikely(! list))
    RESULT_RAISE();

  PyObject *value = KosEvalState_StackPopRef(es);
  if (unlikely(! value))
    RESULT_RAISE();
  int rc = PyList_Append(list, value);
  Py_DECREF(value);
  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   The items list at the depth under the top, read and not written.
   Borrowed; NULL with an exception set on an underrun or a non-list.
*/
KOS_OP_INLINE PyObject *kos_match_items_at(
    KosEvalState *es,
    uint32_t under) {

  PyObject *list = KosEvalState_StackPeekUnder(es, (size_t) under);

  if (unlikely(list && ! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "match items must be a list, not %.200s",
                 Py_TYPE(list)->tp_name);
    return NULL;
  }
  return list;
}


/**
   MATCH_MIN: the items list at field 1 must hold at least field 2
   items. Row LABEL, depth, count.
*/
KOS_MATCH_BODY(kos_match_min) {
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *list = kos_match_items_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! list))
    RESULT_RAISE_AT(es, insn);
  if (PyList_GET_SIZE(list) < (Py_ssize_t) KosExtra_IMM(row, 2))
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_COLUMNS: for each name of the tuple in field 2, the list of
   its values in the passes of the items list at field 0, stored into
   the slot at the same position in the index tuple in field 3 of the
   mark at field 1. Row depth, depth, const, const.
*/
KOS_MATCH_BODY(kos_match_columns) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *list = kos_match_items_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! list))
    RESULT_RAISE();
  PyObject *mark = kos_match_writable_at(es, row, 1);
  if (unlikely(! mark))
    RESULT_RAISE();
  if (unlikely(KosMatchMark_Columns(
          mark, list, KosExtra_OBJ(row, 2), KosExtra_OBJ(row, 3)) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/* === element tests === */


/**
   MATCH_LITERAL: TOS, which stays, must be the constant in field 1.
   Row LABEL, const.
*/
KOS_MATCH_BODY(kos_match_literal) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int r = KosMatch_LiteralTest(elem, KosExtra_OBJ(row, 1));
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_CORE: TOS, which stays, must be an instance of the core type
   whose ordinal is field 1. Row LABEL, ordinal.
*/
KOS_MATCH_BODY(kos_match_core) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int r = KosMatch_CoreTypeTest(elem, KosExtra_IMM(row, 1));
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_TYPE_VAR and MATCH_TYPE_FAST: TOS, which stays, must be an
   instance of the type in the slot in field 1. Row LABEL, slot.
*/
KOS_MATCH_BODY(kos_match_type) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_TYPE_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();
  PyObject *type = kos_match_operand(es, row, 1, fast);
  if (unlikely(! type))
    RESULT_RAISE();

  int r = KosMatch_TypeTest(elem, type);
  Py_DECREF(type);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_CLASS_VAR and MATCH_CLASS_FAST: TOS, which stays, must be an
   instance of the class in the slot in field 1, and the class's
   __match_args__ must then hold at least field 2 names, else a
   TypeError. The tuple is read through the cache slot only after the
   type test passes. Row LABEL, slot, count.
*/
KOS_MATCH_BODY(kos_match_class) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_CLASS_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();
  PyObject *cls = kos_match_operand(es, row, 1, fast);
  if (unlikely(! cls))
    RESULT_RAISE();

  int r = KosMatch_TypeTest(elem, cls);
  if (r > 0)
    r = KosMatch_ClassArgs(
        cls, (Py_ssize_t) KosExtra_IMM(row, 2),
        kos_match_args_cached(insn, cls)) < 0 ? -1 : 1;
  Py_DECREF(cls);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_SAME: TOS, which stays, must equal slot field 2 of the mark
   at field 1, and misses when that slot is empty. Row LABEL, depth,
   index.
*/
KOS_MATCH_BODY(kos_match_same) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *prev = NULL;
  if (unlikely(kos_match_slot_at(
          es, KosExtra_IMM(row, 1), KosExtra_IMM(row, 2), &prev) < 0))
    RESULT_RAISE();

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();
  if (! prev)
    KOS_MATCH_MISS(row);

  int r = KosMatch_SameTest(elem, prev);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/* === projections === */


/**
   MATCH_FIELD_POS_VAR and MATCH_FIELD_POS_FAST: the attribute of
   TOS that __match_args__[field 2] of the class in the slot in field
   1 names, pushed; a missing attribute is a miss, an index past the
   end of __match_args__ a TypeError. Emitted after its CLASS's hit,
   so the tuple is read through the cache slot behind the type test.
   Row LABEL, slot, index.
*/
KOS_MATCH_BODY(kos_match_field_pos) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_FIELD_POS_FAST;
  (void) kos_match_delivery_;

  PyObject *obj = kos_match_peek(es);
  if (unlikely(! obj))
    RESULT_RAISE();
  PyObject *cls = kos_match_operand(es, row, 1, fast);
  if (unlikely(! cls))
    RESULT_RAISE();

  PyObject *val = NULL;
  int r = KosMatch_FieldPos(
      obj, cls, (Py_ssize_t) KosExtra_IMM(row, 2),
      kos_match_args_cached(insn, cls), &val);
  Py_DECREF(cls);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, val);
}


/**
   MATCH_FIELD_KW: the attribute of TOS the constant in field 1
   names, pushed; a missing attribute is a miss. Row LABEL, const.
*/
KOS_MATCH_BODY(kos_match_field_kw) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *obj = kos_match_peek(es);
  if (unlikely(! obj))
    RESULT_RAISE();

  PyObject *val = NULL;
  int r = KosMatch_FieldKw(obj, KosExtra_OBJ(row, 1), &val);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, val);
}


/**
   MATCH_MAPPING: the mapping gate on TOS, which stays. Simple LABEL
   style.
*/
KOS_MATCH_BODY(kos_match_mapping) {
  KOS_SYNC_PC(es, insn);
  (void) kos_match_delivery_;

  PyObject *obj = kos_match_peek(es);
  if (unlikely(! obj))
    RESULT_RAISE();

  int r = KosMatch_MapTest(obj);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    RESULT_JUMP_AT(es, KosOp_TARGET(insn));
  RESULT_NEXT(es);
}


/**
   MATCH_KEY, MATCH_KEY_VAR and MATCH_KEY_FAST: the value under the
   key in field 1 (a constant on MATCH_KEY, a slot on the twins) of
   the mapping on TOS, pushed; an absent key is a miss. Row LABEL,
   key.
*/
KOS_MATCH_BODY(kos_match_key) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  (void) kos_match_delivery_;

  PyObject *obj = kos_match_peek(es);
  if (unlikely(! obj))
    RESULT_RAISE();

  PyObject *key;
  if (op == KOS_OP_MATCH_KEY) {
    key = KosExtra_OBJ(row, 1);
    Py_INCREF(key);
  } else {
    key = kos_match_operand(es, row, 1, op == KOS_OP_MATCH_KEY_FAST);
    if (unlikely(! key))
      RESULT_RAISE();
  }

  PyObject *val = NULL;
  int r = KosMatch_Item(obj, key, &val);
  Py_DECREF(key);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, val);
}


// The end.
