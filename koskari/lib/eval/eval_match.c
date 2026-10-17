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
  Included into eval.c ahead of the opcode handlers. The steps
  themselves are the matcher subsystem's (matcher.h, matcher/step.c);
  a body is the interface between the row, the stack and the slots
  (design/interpreter/matcher-bytecode.md). The MATCH2_* bodies at the
  end are the matcher core's (design/interpreter/matcher-core.md):
  their cursors, marks and items lists are stack entries named by
  depth, and their steps are matcher/cursor.c's and matcher/mark.c's.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/


/* Defined in eval.c after this segment, beside frame_ns_deref and
   frame_ns_store. */
static int frame_ns_clear(KosEvalState *es, PyObject *name);


/* === slot operands === */


/**
   The value of a slot operand: by fast index on a FAST twin, by name
   on a VAR twin. New reference, or NULL with an exception set.
*/
KOS_OP_INLINE PyObject *kos_match_load(
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
   Store an owned value into a slot operand, consuming it on every
   path. 0, or -1 with an exception set.
*/
KOS_OP_INLINE int kos_match_store(
    KosEvalState *es,
    const KosExtra *row,
    int field,
    int fast,
    PyObject *val) {

  if (fast)
    return kos_ns_fast_store_steal(
        KosEvalState_NAMESPACE(es), (size_t) KosExtra_IMM(row, field), val);

  int res = frame_ns_store(es, KosExtra_OBJ(row, field), val);
  Py_DECREF(val);
  return res;
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
   The entry a RAW field names by its depth under the top of the
   stack: a cursor, a mark or a list the walk pushed. Borrowed; NULL
   with an exception set on an underrun.
*/
KOS_OP_INLINE PyObject *kos_match_at(
    KosEvalState *es,
    const KosExtra *row,
    int field) {

  return KosEvalState_StackPeekUnder(es, (size_t) KosExtra_IMM(row, field));
}


/**
   The entry a RAW field names, which must be a cursor: a pair cell,
   nil, or a sequence cursor whose kind is not the mark's. Borrowed;
   NULL with an exception set on an underrun or a mark found there.
*/
KOS_OP_INLINE PyObject *kos_match_cursor_at(
    KosEvalState *es,
    const KosExtra *row,
    int field) {

  PyObject *entry = kos_match_at(es, row, field);

  if (unlikely(KosMatchCursor_Check(entry)
               && KosMatchCursor_KIND(entry) != KOS_MATCH_CURSOR)) {
    PyErr_Format(PyExc_RuntimeError,
                 "match instruction names a mark at depth %u, not a cursor",
                 (unsigned) KosExtra_IMM(row, field));
    return NULL;
  }
  return entry;
}


/**
   The entry a RAW field names, which must be a mark: a pair cell, nil,
   or a sequence mark. Borrowed; NULL with an exception set on an
   underrun or a cursor found there.
*/
KOS_OP_INLINE PyObject *kos_match_mark_at(
    KosEvalState *es,
    const KosExtra *row,
    int field) {

  PyObject *entry = kos_match_at(es, row, field);

  if (unlikely(KosMatchCursor_Check(entry)
               && KosMatchCursor_KIND(entry) != KOS_MATCH_MARK)) {
    PyErr_Format(PyExc_RuntimeError,
                 "match instruction names a cursor at depth %u, not a mark",
                 (unsigned) KosExtra_IMM(row, field));
    return NULL;
  }
  return entry;
}


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


/**
   Advance past a pulled element once it has passed its test: the
   advanced cursor stored into the entry field 1 names; then release
   the step. 0, or -1 with an exception set and the step released.
*/
KOS_OP_INLINE int kos_match_advance(
    KosEvalState *es,
    const KosExtra *row,
    KosMatchStep *step) {

  PyObject *next = KosMatch_StepAdvance(step);
  int res = next ? kos_match_set_at(es, row, 1, next) : -1;

  KosMatch_StepClear(step);
  return res;
}


/* === pull bodies === */


/**
   MATCH_FORM: peek, the sequence gate and length precheck on TOS
   against the widths in field 1; the cursor pushed. Row LABEL, widths.
*/
KOS_MATCH_BODY(kos_match_form) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  PyObject *cursor = NULL;
  int r = KosMatch_Descend(elem, KosExtra_IMM(row, 1), &cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, cursor);
}


/**
   MATCH_NEXT_FORM: pull from the cursor at the depth in field 1, the
   gate on the element against the widths in field 2; the inner cursor
   pushed. Row LABEL, depth, widths.
*/
KOS_MATCH_BODY(kos_match_next_form) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *inner = NULL;
  r = KosMatch_Descend(step.elem, KosExtra_IMM(row, 2), &inner);
  if (r != 1) {
    KosMatch_StepClear(&step);
    if (unlikely(r < 0))
      RESULT_RAISE();
    KOS_MATCH_MISS(row);
  }

  /* the outer entry is written before the inner is pushed, so the
     depth field 1 names still holds */
  if (unlikely(kos_match_advance(es, row, &step) < 0)) {
    Py_DECREF(inner);
    RESULT_RAISE();
  }
  RESULT_STEAL(es, inner);
}


/**
   MATCH_NEXT: pull from the cursor at the depth in field 1 and push
   the element, boxed from a pair spine. Row LABEL, depth.
*/
KOS_MATCH_BODY(kos_match_next) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *boxed = KosMatch_Box(&step);
  if (unlikely(! boxed)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  if (unlikely(kos_match_advance(es, row, &step) < 0)) {
    Py_DECREF(boxed);
    RESULT_RAISE();
  }
  RESULT_STEAL(es, boxed);
}


/**
   MATCH_PEEK: the next element of the cursor at the depth in field 1
   pushed, boxed from a pair spine, the cursor left where it is. Row
   LABEL, depth. The zero-width NOT reads its element this way.
*/
KOS_MATCH_BODY(kos_match_peek_next) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *boxed = KosMatch_Box(&step);
  KosMatch_StepClear(&step);
  RESULT_STEAL(es, boxed);
}


/**
   MATCH_NEXT_BIND: pull from the cursor at the depth in field 1 and
   store the element, boxed from a pair spine, into the slot in field
   2. Row LABEL, depth, slot.
*/
KOS_MATCH_BODY(kos_match_next_bind) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_NEXT_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *boxed = KosMatch_Box(&step);
  if (unlikely(! boxed)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  if (unlikely(kos_match_store(es, row, 2, fast, boxed) < 0)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  if (unlikely(kos_match_advance(es, row, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT_TYPE and MATCH_NEXT_TYPE_BIND: pull from field 1, the
   element must be an instance of the type in field 2; the BIND twins
   store the boxed element into field 3. Row LABEL, slot, slot, slot.
*/
KOS_MATCH_BODY(kos_match_next_type) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int fast = op == KOS_OP_MATCH_NEXT_TYPE_FAST
      || op == KOS_OP_MATCH_NEXT_TYPE_BIND_FAST;
  int bind = op == KOS_OP_MATCH_NEXT_TYPE_BIND_VAR
      || op == KOS_OP_MATCH_NEXT_TYPE_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *type = kos_match_load(es, row, 2, fast);
  if (unlikely(! type)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  r = KosMatch_TypeTest(step.elem, type);
  Py_DECREF(type);
  if (r != 1) {
    KosMatch_StepClear(&step);
    if (unlikely(r < 0))
      RESULT_RAISE();
    KOS_MATCH_MISS(row);
  }

  if (bind) {
    PyObject *boxed = KosMatch_Box(&step);
    if (unlikely(! boxed)) {
      KosMatch_StepClear(&step);
      RESULT_RAISE();
    }
    if (unlikely(kos_match_store(es, row, 3, fast, boxed) < 0)) {
      KosMatch_StepClear(&step);
      RESULT_RAISE();
    }
  }
  if (unlikely(kos_match_advance(es, row, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT_CORE_TYPE and MATCH_NEXT_CORE_TYPE_BIND: pull from field
   1, the element must be an instance of the core type whose ordinal
   is the RAW field 2; the BIND twins store the boxed element into
   field 3. Row LABEL, slot, RAW, slot.
*/
KOS_MATCH_BODY(kos_match_next_core_type) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int fast = op == KOS_OP_MATCH_NEXT_CORE_TYPE_BIND_FAST;
  int bind = op == KOS_OP_MATCH_NEXT_CORE_TYPE_BIND_VAR
      || op == KOS_OP_MATCH_NEXT_CORE_TYPE_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  r = KosMatch_CoreTypeTest(step.elem, KosExtra_IMM(row, 2));
  if (r != 1) {
    KosMatch_StepClear(&step);
    if (unlikely(r < 0))
      RESULT_RAISE();
    KOS_MATCH_MISS(row);
  }

  if (bind) {
    PyObject *boxed = KosMatch_Box(&step);
    if (unlikely(! boxed)) {
      KosMatch_StepClear(&step);
      RESULT_RAISE();
    }
    if (unlikely(kos_match_store(es, row, 3, fast, boxed) < 0)) {
      KosMatch_StepClear(&step);
      RESULT_RAISE();
    }
  }
  if (unlikely(kos_match_advance(es, row, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT_LITERAL: pull from the cursor at the depth in field 1,
   the element must equal the constant in field 2. Row LABEL, depth,
   const.
*/
KOS_MATCH_BODY(kos_match_next_literal) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  r = KosMatch_LiteralTest(step.elem, KosExtra_OBJ(row, 2));
  if (r != 1) {
    KosMatch_StepClear(&step);
    if (unlikely(r < 0))
      RESULT_RAISE();
    KOS_MATCH_MISS(row);
  }
  if (unlikely(kos_match_advance(es, row, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_REST and MATCH_REST_BIND: the remainder of the cursor in
   field 1 but the suffix, the counts packed in field 2; the BIND
   twins store the remainder into field 3. Row LABEL, slot, counts,
   slot.
*/
KOS_MATCH_BODY(kos_match_rest) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int fast = op == KOS_OP_MATCH_REST_BIND_FAST;
  int bind = op == KOS_OP_MATCH_REST_BIND_VAR
      || op == KOS_OP_MATCH_REST_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *value = NULL;
  PyObject *next = NULL;
  int r = KosMatch_Rest(cursor, KosExtra_IMM(row, 2), &value, &next);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (bind) {
    if (unlikely(kos_match_store(es, row, 3, fast, value) < 0)) {
      Py_DECREF(next);
      RESULT_RAISE();
    }
  } else {
    Py_DECREF(value);
  }
  if (unlikely(kos_match_set_at(es, row, 1, next) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_END: the cursor at the depth in field 1 must be at its end.
   With field 2 set the cursor is popped, its extent ending here, and
   the lowering places it on the top; clear, it stays for the captures
   that read it. Row LABEL, depth, flag.
*/
KOS_MATCH_BODY(kos_match_end) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();
  if (! KosMatch_End(cursor))
    KOS_MATCH_MISS(row);
  if (KosExtra_IMM(row, 2)
      && unlikely(KosEvalState_StackDropNoErr(es, 1) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_AVAILABLE: the cursor at the depth in field 1 must have at
   least the count in field 2 of elements left. Row LABEL, depth, RAW.
*/
KOS_MATCH_BODY(kos_match_available) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();
  if (! KosMatch_Available(cursor, (Py_ssize_t) KosExtra_IMM(row, 2)))
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/* === peek bodies === */


/**
   MATCH_LITERAL: TOS must equal the constant in field 1. Row LABEL,
   const.
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
   MATCH_TYPE and MATCH_TYPE_BIND: TOS must be an instance of the type
   in field 1; the BIND twins store TOS into field 2. Row LABEL, slot,
   slot.
*/
KOS_MATCH_BODY(kos_match_type) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int fast = op == KOS_OP_MATCH_TYPE_FAST
      || op == KOS_OP_MATCH_TYPE_BIND_FAST;
  int bind = op == KOS_OP_MATCH_TYPE_BIND_VAR
      || op == KOS_OP_MATCH_TYPE_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  PyObject *type = kos_match_load(es, row, 1, fast);
  if (unlikely(! type))
    RESULT_RAISE();
  int r = KosMatch_TypeTest(elem, type);
  Py_DECREF(type);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (bind) {
    Py_INCREF(elem);
    if (unlikely(kos_match_store(es, row, 2, fast, elem) < 0))
      RESULT_RAISE();
  }
  RESULT_NEXT(es);
}


/**
   MATCH_CORE_TYPE and MATCH_CORE_TYPE_BIND: TOS must be an instance of
   the core type whose ordinal is the RAW field 1; the BIND twins store
   TOS into field 2. Row LABEL, RAW, slot.
*/
KOS_MATCH_BODY(kos_match_core_type) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int fast = op == KOS_OP_MATCH_CORE_TYPE_BIND_FAST;
  int bind = op != KOS_OP_MATCH_CORE_TYPE;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int r = KosMatch_CoreTypeTest(elem, KosExtra_IMM(row, 1));
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (bind) {
    Py_INCREF(elem);
    if (unlikely(kos_match_store(es, row, 2, fast, elem) < 0))
      RESULT_RAISE();
  }
  RESULT_NEXT(es);
}


/**
   MATCH_BIND: TOS stored into the slot the arg names, nothing popped.
   Simple VAR or FASTVAR style.
*/
KOS_MATCH_BODY(kos_match_bind) {
  KOS_SYNC_PC(es, insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int res;
  if (fast) {
    Py_INCREF(elem);
    res = kos_ns_fast_store_steal(
        KosEvalState_NAMESPACE(es), (size_t) KosOp_ARG(insn), elem);
  } else {
    res = frame_ns_store(es, KosOp_OBJ(insn), elem);
  }
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_CLASS: TOS must be an instance of the class in field 1 whose
   __match_args__ covers the positional count in field 2. Row LABEL,
   slot, count.
*/
KOS_MATCH_BODY(kos_match_class) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_CLASS_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  PyObject *cls = kos_match_load(es, row, 1, fast);
  if (unlikely(! cls))
    RESULT_RAISE();
  int r = KosMatch_ClassTest(elem, cls, (Py_ssize_t) KosExtra_IMM(row, 2));
  Py_DECREF(cls);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_FIELD_POS and MATCH_FIELD_POS_BIND: the attribute of TOS that
   __match_args__[field 2] of the class in field 1 names, pushed, or
   stored into field 3 by the BIND twins. Row LABEL, slot, count, slot.
*/
KOS_MATCH_BODY(kos_match_field_pos) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int fast = op == KOS_OP_MATCH_FIELD_POS_FAST
      || op == KOS_OP_MATCH_FIELD_POS_BIND_FAST;
  int bind = op == KOS_OP_MATCH_FIELD_POS_BIND_VAR
      || op == KOS_OP_MATCH_FIELD_POS_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  PyObject *cls = kos_match_load(es, row, 1, fast);
  if (unlikely(! cls))
    RESULT_RAISE();
  PyObject *val = NULL;
  int r = KosMatch_FieldPos(
      elem, cls, (Py_ssize_t) KosExtra_IMM(row, 2), &val);
  Py_DECREF(cls);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (bind) {
    if (unlikely(kos_match_store(es, row, 3, fast, val) < 0))
      RESULT_RAISE();
    RESULT_NEXT(es);
  }
  RESULT_STEAL(es, val);
}


/**
   MATCH_FIELD_KW and MATCH_FIELD_KW_BIND: the attribute of TOS that
   the constant in field 1 names, pushed, or stored into field 2 by
   the BIND twins. Row LABEL, const, slot.
*/
KOS_MATCH_BODY(kos_match_field_kw) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int fast = op == KOS_OP_MATCH_FIELD_KW_BIND_FAST;
  int bind = op == KOS_OP_MATCH_FIELD_KW_BIND_VAR
      || op == KOS_OP_MATCH_FIELD_KW_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  PyObject *val = NULL;
  int r = KosMatch_FieldKw(elem, KosExtra_OBJ(row, 1), &val);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (bind) {
    if (unlikely(kos_match_store(es, row, 2, fast, val) < 0))
      RESULT_RAISE();
    RESULT_NEXT(es);
  }
  RESULT_STEAL(es, val);
}


/**
   MATCH_MAP: the mapping gate on TOS. Simple LABEL style.
*/
KOS_MATCH_BODY(kos_match_map) {
  KOS_SYNC_PC(es, insn);
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int r = KosMatch_MapTest(elem);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    RESULT_JUMP_AT(es, KosOp_TARGET(insn));
  RESULT_NEXT(es);
}


/**
   The MATCH_KEY family: the value under the key in field 1 (a
   constant on the CONST forms, a slot otherwise) of the mapping on
   TOS, pushed, or stored into field 2 by the BIND forms. Row LABEL,
   key, slot.
*/
KOS_MATCH_BODY(kos_match_key) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int key_const = op == KOS_OP_MATCH_KEY_CONST
      || op == KOS_OP_MATCH_KEY_CONST_BIND_VAR
      || op == KOS_OP_MATCH_KEY_CONST_BIND_FAST;
  int fast = op == KOS_OP_MATCH_KEY_FAST
      || op == KOS_OP_MATCH_KEY_CONST_BIND_FAST
      || op == KOS_OP_MATCH_KEY_FAST_BIND_FAST;
  int bind = op == KOS_OP_MATCH_KEY_CONST_BIND_VAR
      || op == KOS_OP_MATCH_KEY_CONST_BIND_FAST
      || op == KOS_OP_MATCH_KEY_VAR_BIND_VAR
      || op == KOS_OP_MATCH_KEY_FAST_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  PyObject *key;
  if (key_const) {
    key = KosExtra_OBJ(row, 1);
    Py_INCREF(key);
  } else {
    key = kos_match_load(es, row, 1, fast);
    if (unlikely(! key))
      RESULT_RAISE();
  }

  PyObject *val = NULL;
  int r = KosMatch_Item(elem, key, &val);
  Py_DECREF(key);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (bind) {
    if (unlikely(kos_match_store(es, row, 2, fast, val) < 0))
      RESULT_RAISE();
    RESULT_NEXT(es);
  }
  RESULT_STEAL(es, val);
}


/* === the capture-reading bodies === */


/**
   The value in a fast slot, or none, without the NameError the deref
   raises for an empty slot: the raw slot's own value, a plain ref's
   value, and the general deref only for a ref kind the peek cannot
   read. Returns a new reference, or NULL with *empty set and no
   exception when the slot is unbound, or NULL with an exception set.
*/
KOS_OP_INLINE PyObject *kos_match_fast_value(
    KosEvalState *es,
    size_t idx,
    int *empty) {

  PyObject *ns = KosEvalState_NAMESPACE(es);
  int raw;
  PyObject *val = kos_ns_fast_peek_raw(ns, idx, &raw);

  *empty = 0;
  if (val) {
    Py_INCREF(val);
    return val;
  }
  if (raw || ! KosNamespace_GET_FAST_VAL(ns, idx)) {
    *empty = 1;
    return NULL;
  }
  val = kos_ns_fast_deref(ns, idx);
  if (! val && PyErr_ExceptionMatches(PyExc_NameError)) {
    PyErr_Clear();
    *empty = 1;
  }
  return val;
}


/**
   The value bound in a slot operand, or none: by fast index on a FAST
   twin, by name on a VAR twin. Returns a new reference, or NULL with
   *empty set and no exception when the slot is unbound, or NULL with
   an exception set.
*/
KOS_OP_INLINE PyObject *kos_match_load_bound(
    KosEvalState *es,
    const KosExtra *row,
    int field,
    int fast,
    int *empty) {

  if (fast)
    return kos_match_fast_value(es, (size_t) KosExtra_IMM(row, field), empty);

  *empty = 0;
  PyObject *val = frame_ns_deref(es, KosExtra_OBJ(row, field));
  if (val)
    return val;
  if (PyErr_ExceptionMatches(PyExc_NameError)) {
    PyErr_Clear();
    *empty = 1;
  }
  return NULL;
}


/**
   MATCH_SAME: TOS must equal the value bound in field 1; an unbound
   slot is a miss. Row LABEL, slot.
*/
KOS_MATCH_BODY(kos_match_same) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_SAME_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int empty;
  PyObject *prev = kos_match_load_bound(es, row, 1, fast, &empty);
  if (! prev) {
    if (empty)
      KOS_MATCH_MISS(row);
    RESULT_RAISE();
  }
  int r = KosMatch_SameTest(elem, prev);
  Py_DECREF(prev);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT_SAME: pull from the cursor at the depth in field 1; the
   element must equal the value bound in the slot in field 2. Row
   LABEL, depth, slot.
*/
KOS_MATCH_BODY(kos_match_next_same) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_NEXT_SAME_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  int empty;
  PyObject *prev = kos_match_load_bound(es, row, 2, fast, &empty);
  if (! prev) {
    KosMatch_StepClear(&step);
    if (empty)
      KOS_MATCH_MISS(row);
    RESULT_RAISE();
  }
  r = KosMatch_SameTest(step.elem, prev);
  Py_DECREF(prev);
  if (r != 1) {
    KosMatch_StepClear(&step);
    if (unlikely(r < 0))
      RESULT_RAISE();
    KOS_MATCH_MISS(row);
  }
  if (unlikely(kos_match_advance(es, row, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT_APPEND: pull from the cursor at the depth in field 1 and
   append the boxed element to the list at the depth in field 2. Row
   LABEL, depth, depth.
*/
KOS_MATCH_BODY(kos_match_next_append) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *list = kos_match_list_at(es, (size_t) KosExtra_IMM(row, 2));
  if (unlikely(! list)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  PyObject *boxed = KosMatch_Box(&step);
  int rc = boxed ? PyList_Append(list, boxed) : -1;
  Py_XDECREF(boxed);
  if (unlikely(rc < 0)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  if (unlikely(kos_match_advance(es, row, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_COLLECT: every remaining element of the cursor at the depth in
   field 1, boxed from a pair spine, appended to the list at the depth
   in field 2; an improper tail is a miss. Row LABEL, depth, depth.
*/
KOS_MATCH_BODY(kos_match_collect) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 1);
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *list = kos_match_list_at(es, (size_t) KosExtra_IMM(row, 2));
  if (unlikely(! list))
    RESULT_RAISE();

  PyObject *next = NULL;
  int r = KosMatch_Collect(cursor, list, &next);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  if (unlikely(kos_match_set_at(es, row, 1, next) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_MARK: a mark at the position of the cursor at the depth in
   field 0 pushed. Row depth.
*/
KOS_MATCH_BODY(kos_match_mark) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 0);
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *mark = KosMatch_Mark(cursor);
  if (unlikely(! mark))
    RESULT_RAISE();
  RESULT_STEAL(es, mark);
}


/**
   MATCH_REMARK: the mark at the depth in field 1 moved to the
   position of the cursor at the depth in field 0. Row depth, depth.
*/
KOS_MATCH_BODY(kos_match_remark) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 0);
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *mark = kos_match_mark_at(es, row, 1);
  if (unlikely(! mark))
    RESULT_RAISE();
  PyObject *moved = KosMatch_Remark(cursor, mark);
  if (unlikely(! moved))
    RESULT_RAISE();
  if (unlikely(kos_match_set_at(es, row, 1, moved) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_RESET: the cursor at the depth in field 0 moved back to the
   position of the mark at the depth in field 1. Row depth, depth.
*/
KOS_MATCH_BODY(kos_match_reset) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_cursor_at(es, row, 0);
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *mark = kos_match_mark_at(es, row, 1);
  if (unlikely(! mark))
    RESULT_RAISE();
  PyObject *moved = KosMatch_Reset(cursor, mark);
  if (unlikely(! moved))
    RESULT_RAISE();
  if (unlikely(kos_match_set_at(es, row, 0, moved) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/* Capture slots gathered into an owned array before spilling to the
   heap. */
#define KOS_MATCH_GATHER_INLINE 8


/**
   Gather the bound values of the names into an owned array: by fast
   index from base on the slot-run forms, by name on the NAMED forms,
   NULL for an unbound slot. Returns the array (freed by the caller
   with kos_match_release_values when it is not buf), or NULL with an
   exception set.
*/
static PyObject **kos_match_gather_values(
    KosEvalState *es,
    PyObject *names,
    int by_name,
    uint32_t base,
    PyObject **buf) {

  Py_ssize_t n = PyTuple_GET_SIZE(names);
  PyObject **values = buf;

  if (n > KOS_MATCH_GATHER_INLINE) {
    values = PyMem_Malloc((size_t) n * sizeof(PyObject *));
    if (! values) {
      PyErr_NoMemory();
      return NULL;
    }
  }
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *val;
    int empty = 0;
    if (by_name) {
      val = frame_ns_deref(es, PyTuple_GET_ITEM(names, i));
      if (! val && PyErr_ExceptionMatches(PyExc_NameError)) {
        PyErr_Clear();
        empty = 1;
      }
    } else {
      val = kos_match_fast_value(es, base + (size_t) i, &empty);
    }
    if (! val && ! empty) {
      for (Py_ssize_t j = 0; j < i; j++)
        Py_XDECREF(values[j]);
      if (values != buf)
        PyMem_Free(values);
      return NULL;
    }
    values[i] = val;
  }
  return values;
}


/**
   Clear the slots an iteration read, by index from base or by name,
   so the next iteration starts with none of them bound. 0, or -1 with
   an exception set.
*/
static int kos_match_clear_values(
    KosEvalState *es,
    PyObject *names,
    int by_name,
    uint32_t base) {

  Py_ssize_t n = PyTuple_GET_SIZE(names);

  for (Py_ssize_t i = 0; i < n; i++) {
    int rc;
    if (by_name)
      rc = frame_ns_clear(es, PyTuple_GET_ITEM(names, i));
    else
      rc = KosNamespace_FastClear(
          KosEvalState_NAMESPACE(es), base + (size_t) i);
    if (rc < 0)
      return -1;
  }
  return 0;
}


static void kos_match_release_values(
    PyObject **values,
    Py_ssize_t n,
    PyObject **buf) {

  for (Py_ssize_t i = 0; i < n; i++)
    Py_XDECREF(values[i]);
  if (values != buf)
    PyMem_Free(values);
}


/**
   The MATCH_CAPTURES and MATCH_ITERATION families: the names tuple in
   field 0; on the slot-run forms the first capture slot in field 1,
   then the depths; on the NAMED forms the depths from field 1, the
   captures read by name. A captures row names the start and the
   cursor by depth and pushes the captures. An iteration runs with its
   mark on the top and its list under that, names the cursor alone,
   and its last field is the mode: 0 pushes the value, 1 appends it to
   the list.
*/
KOS_MATCH_BODY(kos_match_captures) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int iteration = op == KOS_OP_MATCH_ITERATION
      || op == KOS_OP_MATCH_ITERATION_NAMED;
  int by_name = op != KOS_OP_MATCH_CAPTURES && op != KOS_OP_MATCH_ITERATION;
  int field = by_name ? 1 : 2;
  (void) kos_match_delivery_;

  /* the slot-run forms name the keys; the NAMED forms carry (keys,
     locals), the locals being the names the captures are read by */
  PyObject *table = KosExtra_OBJ(row, 0);
  PyObject *names = by_name ? PyTuple_GET_ITEM(table, 0) : table;
  PyObject *locals = by_name ? PyTuple_GET_ITEM(table, 1) : table;

  /* the captures row names its start and cursor by depth; an
     iteration runs with its mark on the top and its list under that,
     so it names the cursor alone and carries a mode */
  PyObject *start;
  PyObject *cursor;
  int append = 0;
  if (iteration) {
    start = KosEvalState_StackPeekUnder(es, 0);
    if (unlikely(start && KosMatchCursor_Check(start)
                 && KosMatchCursor_KIND(start) != KOS_MATCH_MARK)) {
      PyErr_SetString(PyExc_RuntimeError,
                      "match iteration finds a cursor on top, not its mark");
      RESULT_RAISE();
    }
    cursor = kos_match_cursor_at(es, row, field);
    append = KosExtra_IMM(row, field + 1) == 1;
  } else {
    start = kos_match_mark_at(es, row, field);
    cursor = kos_match_cursor_at(es, row, field + 1);
  }
  if (unlikely(! start || ! cursor))
    RESULT_RAISE();

  PyObject *buf[KOS_MATCH_GATHER_INLINE];
  PyObject **values = kos_match_gather_values(
      es, locals, by_name, by_name ? 0 : KosExtra_IMM(row, 1), buf);
  if (unlikely(! values))
    RESULT_RAISE();

  PyObject *out = iteration
      ? KosMatch_Iteration(names, values, start, cursor)
      : KosMatch_Captures(names, values, start, cursor);
  kos_match_release_values(values, PyTuple_GET_SIZE(names), buf);
  if (unlikely(! out))
    RESULT_RAISE();

  /* an iteration leaves its slots clear for the next one */
  if (iteration && unlikely(kos_match_clear_values(
          es, locals, by_name, by_name ? 0 : KosExtra_IMM(row, 1)) < 0)) {
    Py_DECREF(out);
    RESULT_RAISE();
  }
  if (! append)
    RESULT_STEAL(es, out);

  PyObject *list = kos_match_list_at(es, 1);
  if (unlikely(! list)) {
    Py_DECREF(out);
    RESULT_RAISE();
  }
  int rc = PyList_Append(list, out);
  Py_DECREF(out);
  if (unlikely(rc < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}



/**
   MATCH_COUNT: the list at the depth in field 1 must hold at least the
   count in field 2 of items. Row LABEL, RAW, RAW.
*/
KOS_MATCH_BODY(kos_match_count) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *list = kos_match_at(es, row, 1);
  if (unlikely(! list))
    RESULT_RAISE();
  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "match count requires a list, not %.200s",
                 Py_TYPE(list)->tp_name);
    RESULT_RAISE();
  }
  if (PyList_GET_SIZE(list) < (Py_ssize_t) KosExtra_IMM(row, 2))
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/* === the matcher core: entries by depth === */


/**
   The cursor at the depth under the top, as the instruction found the
   stack. Borrowed; NULL with an exception set on an underrun,
   SystemError when the entry is not a cursor.
*/
KOS_OP_INLINE PyObject *kos_match2_cursor_at(
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
KOS_OP_INLINE PyObject *kos_match2_mark_at(
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
KOS_OP_INLINE int kos_match2_slot_at(
    KosEvalState *es,
    uint32_t under,
    uint32_t i,
    PyObject **out) {

  PyObject *mark = kos_match2_mark_at(es, under);

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
KOS_OP_INLINE PyObject *kos_match2_writable_at(
    KosEvalState *es,
    const KosExtra *row,
    int field) {

  PyObject *mark = kos_match2_mark_at(es, KosExtra_IMM(row, field));

  if (unlikely(! mark))
    return NULL;

  PyObject *target = KosMatchMark_Writable(mark, 1);
  if (unlikely(! target))
    return NULL;
  if (unlikely(kos_match_set_at(es, row, field, target) < 0))
    return NULL;
  return target;
}


/* === the matcher core: cursors === */


/**
   MATCH2_DESCEND: the sequence gate on TOS read through Datum; the
   cursor over it replaces it. A miss pops TOS and jumps. Simple LABEL
   style.
*/
KOS_MATCH_BODY(kos_match2_descend) {
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
   MATCH2_SINGLE: a one-element cursor over TOS replaces it. No arg.
*/
KOS_MATCH_BODY(kos_match2_single) {
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
   MATCH2_FITS: the cursor at field 1 must have at least field 2 and
   at most field 3 elements left, KOS_MATCH_UNBOUNDED for no maximum.
   Row LABEL, depth, min, max.
*/
KOS_MATCH_BODY(kos_match2_fits) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE();

  Py_ssize_t remaining = KosMatchCursor_Remaining(cursor);
  uint32_t max = KosExtra_IMM(row, 3);
  if (remaining < (Py_ssize_t) KosExtra_IMM(row, 2)
      || (max != KOS_MATCH_UNBOUNDED && remaining > (Py_ssize_t) max))
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH2_NEXT: the next element of the cursor at field 1 pushed, the
   moved cursor stored back; a miss at the end. Row LABEL, depth.
*/
KOS_MATCH_BODY(kos_match2_next) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *elem = NULL;
  PyObject *next = NULL;
  int r = KosMatchCursor_Next(cursor, 1, &elem, &next);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (unlikely(kos_match_set_at(es, row, 1, next) < 0)) {
    Py_DECREF(elem);
    RESULT_RAISE();
  }
  RESULT_STEAL(es, elem);
}


/**
   MATCH2_BOUND: a cursor at the position of the cursor at field 1,
   ending field 2 elements before its end, pushed; a miss when fewer
   remain. Row LABEL, depth, width.
*/
KOS_MATCH_BODY(kos_match2_bound) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 1));
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
   MATCH2_SYNC: the cursor at field 0 moved to the position of the
   cursor at field 1. Row depth, depth.
*/
KOS_MATCH_BODY(kos_match2_sync) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *to = kos_match2_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! to))
    RESULT_RAISE();

  PyObject *moved = KosMatchCursor_Sync(cursor, to, 1);
  if (unlikely(! moved))
    RESULT_RAISE();
  if (unlikely(kos_match_set_at(es, row, 0, moved) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH2_TAIL: the remaining elements of the cursor at field 1 less
   field 2 pushed as a span, the cursor advanced past them; a miss
   when fewer than field 2 remain or the span is shorter than field 3.
   Row LABEL, depth, width, min.
*/
KOS_MATCH_BODY(kos_match2_tail) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 1));
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

  if (unlikely(kos_match_set_at(es, row, 1, next) < 0)) {
    Py_DECREF(span);
    RESULT_RAISE();
  }
  RESULT_STEAL(es, span);
}


/* === the matcher core: marks === */


/**
   MATCH2_SCOPE: a mark at the position of the cursor at field 0 with
   field 1 empty slots pushed. Row depth, count.
*/
KOS_MATCH_BODY(kos_match2_scope) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  RESULT_STEAL(
      es, KosMatchMark_New(cursor, (Py_ssize_t) KosExtra_IMM(row, 1)));
}


/**
   MATCH2_RESET: the cursor at field 0 moved back to the position of
   the mark at field 1. Row depth, depth.
*/
KOS_MATCH_BODY(kos_match2_reset) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *mark = kos_match2_mark_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! mark))
    RESULT_RAISE();

  PyObject *moved = KosMatchMark_Reset(cursor, mark, 1);
  if (unlikely(! moved))
    RESULT_RAISE();
  if (unlikely(kos_match_set_at(es, row, 0, moved) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH2_AT: the element at the position of the mark at field 0
   pushed. Row depth.
*/
KOS_MATCH_BODY(kos_match2_at) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *mark = kos_match2_mark_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! mark))
    RESULT_RAISE();
  RESULT_STEAL(es, KosMatchMark_At(mark));
}


/**
   MATCH2_SPAN: the span from the position of the mark at field 1 to
   the cursor at field 2 pushed; an empty span jumps to field 0,
   pushing nothing. Row LABEL, depth, depth.
*/
KOS_MATCH_BODY(kos_match2_span) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *mark = kos_match2_mark_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! mark))
    RESULT_RAISE();
  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 2));
  if (unlikely(! cursor))
    RESULT_RAISE();

  if (KosMatchCursor_INDEX(cursor) == KosMatchMark_INDEX(mark))
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, KosMatchMark_Span(mark, cursor));
}


/**
   MATCH2_STORE: TOS popped into slot field 1 of the mark at field 0;
   when the cursor at field 2 walks a pair spine, a Datum value takes
   the slot's name from the names tuple in field 3 as its binding. Row
   depth, index, depth, const.
*/
KOS_MATCH_BODY(kos_match2_store) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  Py_ssize_t i = (Py_ssize_t) KosExtra_IMM(row, 1);
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 2));
  if (unlikely(! cursor))
    RESULT_RAISE();

  /* the code's validation holds the index within the names */
  PyObject *name = KosMatchCursor_KIND(cursor) == KOS_MATCH_SPINE
      ? PyTuple_GET_ITEM(KosExtra_OBJ(row, 3), i) : NULL;

  PyObject *mark = kos_match2_writable_at(es, row, 0);
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
   MATCH2_CLEAR: the slots the index tuple in field 1 names emptied in
   the mark at field 0. Row depth, const.
*/
KOS_MATCH_BODY(kos_match2_clear) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *mark = kos_match2_writable_at(es, row, 0);
  if (unlikely(! mark))
    RESULT_RAISE();
  if (unlikely(KosMatchMark_Clear(mark, KosExtra_OBJ(row, 1)) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH2_CAPTURES and MATCH2_PASS: the captures of the mark at field
   0, keyed by the names tuple in field 2, over the span to the cursor
   at field 1, pushed; PASS pushes a REPEAT pass's value instead, the
   captures in their MATCH2_CAPTURES form when field 3 is set. Row
   depth, depth, const, flag.
*/
KOS_MATCH_BODY(kos_match2_captures) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int pass = KosOp_OP(insn) == KOS_OP_MATCH2_PASS;
  (void) kos_match_delivery_;

  PyObject *mark = kos_match2_mark_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! mark))
    RESULT_RAISE();
  PyObject *cursor = kos_match2_cursor_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *names = KosExtra_OBJ(row, 2);
  RESULT_STEAL(es, pass
               ? KosMatchMark_Pass(
                   mark, cursor, names, KosExtra_IMM(row, 3) != 0)
               : KosMatchMark_Captures(mark, cursor, names));
}


/**
   MATCH2_LOAD: slot field 2 of the mark at field 1 pushed; an empty
   slot jumps to field 0, pushing nothing. Row LABEL, depth, index.
*/
KOS_MATCH_BODY(kos_match2_load) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *value = NULL;
  if (unlikely(kos_match2_slot_at(
          es, KosExtra_IMM(row, 1), KosExtra_IMM(row, 2), &value) < 0))
    RESULT_RAISE();
  if (! value)
    KOS_MATCH_MISS(row);
  RESULT_PUSH(es, value);
}


/* === the matcher core: items === */


/**
   MATCH2_APPEND: TOS popped and appended to the items list at field
   0, which its entry alone holds once kos_match_list_at has copied a
   shared one. Row depth.
*/
KOS_MATCH_BODY(kos_match2_append) {
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
KOS_OP_INLINE PyObject *kos_match2_items_at(
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
   MATCH2_MIN: the items list at field 1 must hold at least field 2
   items. Row LABEL, depth, count.
*/
KOS_MATCH_BODY(kos_match2_min) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *list = kos_match2_items_at(es, KosExtra_IMM(row, 1));
  if (unlikely(! list))
    RESULT_RAISE();
  if (PyList_GET_SIZE(list) < (Py_ssize_t) KosExtra_IMM(row, 2))
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH2_COLUMNS: for each name of the tuple in field 2, the list of
   its values in the passes of the items list at field 0, stored into
   the slot at the same position in the index tuple in field 3 of the
   mark at field 1. Row depth, depth, const, const.
*/
KOS_MATCH_BODY(kos_match2_columns) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *list = kos_match2_items_at(es, KosExtra_IMM(row, 0));
  if (unlikely(! list))
    RESULT_RAISE();
  PyObject *mark = kos_match2_writable_at(es, row, 1);
  if (unlikely(! mark))
    RESULT_RAISE();
  if (unlikely(KosMatchMark_Columns(
          mark, list, KosExtra_OBJ(row, 2), KosExtra_OBJ(row, 3)) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/* === the matcher core: element tests === */


/**
   MATCH2_LITERAL: TOS popped; it must be the constant in field 1. Row
   LABEL, const.
*/
KOS_MATCH_BODY(kos_match2_literal) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *elem = KosEvalState_StackPopRef(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int r = KosMatch_LiteralTest(elem, KosExtra_OBJ(row, 1));
  Py_DECREF(elem);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH2_CORE: TOS popped; it must be an instance of the core type
   whose ordinal is field 1. Row LABEL, ordinal.
*/
KOS_MATCH_BODY(kos_match2_core) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *elem = KosEvalState_StackPopRef(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  int r = KosMatch_CoreTypeTest(elem, KosExtra_IMM(row, 1));
  Py_DECREF(elem);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH2_TYPE_VAR and MATCH2_TYPE_FAST: TOS popped; it must be an
   instance of the type in the slot in field 1. Row LABEL, slot.
*/
KOS_MATCH_BODY(kos_match2_type) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH2_TYPE_FAST;
  (void) kos_match_delivery_;

  PyObject *type = kos_match_load(es, row, 1, fast);
  if (unlikely(! type))
    RESULT_RAISE();
  PyObject *elem = KosEvalState_StackPopRef(es);
  if (unlikely(! elem)) {
    Py_DECREF(type);
    RESULT_RAISE();
  }

  int r = KosMatch_TypeTest(elem, type);
  Py_DECREF(elem);
  Py_DECREF(type);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH2_SAME: TOS popped; it must equal slot field 2 of the mark at
   field 1, and misses when that slot is empty. Row LABEL, depth,
   index.
*/
KOS_MATCH_BODY(kos_match2_same) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  (void) kos_match_delivery_;

  PyObject *prev = NULL;
  if (unlikely(kos_match2_slot_at(
          es, KosExtra_IMM(row, 1), KosExtra_IMM(row, 2), &prev) < 0))
    RESULT_RAISE();

  PyObject *elem = KosEvalState_StackPopRef(es);
  if (unlikely(! elem))
    RESULT_RAISE();
  if (! prev) {
    Py_DECREF(elem);
    KOS_MATCH_MISS(row);
  }

  /* the comparison may run user code; the value is held across it */
  Py_INCREF(prev);
  int r = KosMatch_SameTest(elem, prev);
  Py_DECREF(prev);
  Py_DECREF(elem);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/* === the matcher core: projections === */


/**
   MATCH2_FIELD_POS_VAR and MATCH2_FIELD_POS_FAST: the attribute of
   TOS that __match_args__[field 2] of the class in the slot in field
   1 names, pushed; a missing attribute is a miss, an index past the
   end of __match_args__ a TypeError. Row LABEL, slot, index.
*/
KOS_MATCH_BODY(kos_match2_field_pos) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH2_FIELD_POS_FAST;
  (void) kos_match_delivery_;

  PyObject *obj = kos_match_peek(es);
  if (unlikely(! obj))
    RESULT_RAISE();
  PyObject *cls = kos_match_load(es, row, 1, fast);
  if (unlikely(! cls))
    RESULT_RAISE();

  PyObject *val = NULL;
  int r = KosMatch_FieldPos(
      obj, cls, (Py_ssize_t) KosExtra_IMM(row, 2), &val);
  Py_DECREF(cls);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_STEAL(es, val);
}


/**
   MATCH2_FIELD_KW: the attribute of TOS the constant in field 1
   names, pushed; a missing attribute is a miss. Row LABEL, const.
*/
KOS_MATCH_BODY(kos_match2_field_kw) {
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
   MATCH2_MAPPING: the mapping gate on TOS, which stays. Simple LABEL
   style.
*/
KOS_MATCH_BODY(kos_match2_mapping) {
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
   MATCH2_KEY, MATCH2_KEY_VAR and MATCH2_KEY_FAST: the value under the
   key in field 1 (a constant on MATCH2_KEY, a slot on the twins) of
   the mapping on TOS, pushed; an absent key is a miss. Row LABEL,
   key.
*/
KOS_MATCH_BODY(kos_match2_key) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  (void) kos_match_delivery_;

  PyObject *obj = kos_match_peek(es);
  if (unlikely(! obj))
    RESULT_RAISE();

  PyObject *key;
  if (op == KOS_OP_MATCH2_KEY) {
    key = KosExtra_OBJ(row, 1);
    Py_INCREF(key);
  } else {
    key = kos_match_load(es, row, 1, op == KOS_OP_MATCH2_KEY_FAST);
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
