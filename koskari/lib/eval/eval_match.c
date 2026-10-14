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

  The bodies of the MATCH_* instructions, shared by each VAR and FAST
  twin: the twin tail-calls its body, and the body reads which it is
  off the opcode to resolve its slot operands by name or by index.
  Included into eval.c ahead of the opcode handlers. The steps
  themselves are the matcher subsystem's (matcher.h, matcher/step.c);
  a body is the interface between the row, the stack and the slots
  (design/interpreter/matcher-bytecode.md).

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
   Advance past a pulled element once it has passed its test: the
   advanced pair cursor stored back into field 1, or the sequence
   cursor stepped in place; then release the step. 0, or -1 with an
   exception set and the step released.
*/
KOS_OP_INLINE int kos_match_advance(
    KosEvalState *es,
    const KosExtra *row,
    int fast,
    KosMatchStep *step) {

  int res = 0;

  if (step->next) {
    PyObject *next = step->next;
    step->next = NULL;
    res = kos_match_store(es, row, 1, fast, next);
  } else {
    KosMatch_StepAdvance(step);
  }
  KosMatch_StepClear(step);
  return res;
}


/* === pull bodies === */


/**
   MATCH_FORM: peek, the sequence gate and length precheck on TOS, the
   cursor into field 1. Row LABEL, slot, widths.
*/
KOS_MATCH_BODY(kos_match_form) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_FORM_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = kos_match_peek(es);
  if (unlikely(! elem))
    RESULT_RAISE();

  PyObject *cursor = NULL;
  int r = KosMatch_Descend(elem, KosExtra_IMM(row, 2), &cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (unlikely(kos_match_store(es, row, 1, fast, cursor) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT_FORM: pull from field 1, the gate on the element, the
   inner cursor into field 2. Row LABEL, slot, slot, widths.
*/
KOS_MATCH_BODY(kos_match_next_form) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_NEXT_FORM_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *inner = NULL;
  r = KosMatch_Descend(step.elem, KosExtra_IMM(row, 3), &inner);
  if (r != 1) {
    KosMatch_StepClear(&step);
    if (unlikely(r < 0))
      RESULT_RAISE();
    KOS_MATCH_MISS(row);
  }

  if (unlikely(kos_match_store(es, row, 2, fast, inner) < 0)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  if (unlikely(kos_match_advance(es, row, fast, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT: pull from field 1 and push the element, boxed from a
   pair spine. Row LABEL, slot.
*/
KOS_MATCH_BODY(kos_match_next) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_NEXT_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *boxed = KosMatch_Box(&step);
  if (unlikely(! boxed)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  if (unlikely(kos_match_advance(es, row, fast, &step) < 0)) {
    Py_DECREF(boxed);
    RESULT_RAISE();
  }
  RESULT_STEAL(es, boxed);
}


/**
   MATCH_PEEK: the next element of the cursor in field 1 pushed, boxed
   from a pair spine, the cursor left where it is. Row LABEL, slot.
   The zero-width NOT reads its element this way, since a sequence
   cursor advances in place and a saved copy of it would advance too.
*/
KOS_MATCH_BODY(kos_match_peek_next) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_PEEK_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *boxed = KosMatch_Box(&step);
  KosMatch_StepClear(&step);
  RESULT_STEAL(es, boxed);
}


/**
   MATCH_NEXT_BIND: pull from field 1 and store the element, boxed
   from a pair spine, into field 2. Row LABEL, slot, slot.
*/
KOS_MATCH_BODY(kos_match_next_bind) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_NEXT_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
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
  if (unlikely(kos_match_advance(es, row, fast, &step) < 0))
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

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
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
  if (unlikely(kos_match_advance(es, row, fast, &step) < 0))
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
  int fast = op == KOS_OP_MATCH_NEXT_CORE_TYPE_FAST
      || op == KOS_OP_MATCH_NEXT_CORE_TYPE_BIND_FAST;
  int bind = op == KOS_OP_MATCH_NEXT_CORE_TYPE_BIND_VAR
      || op == KOS_OP_MATCH_NEXT_CORE_TYPE_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
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
  if (unlikely(kos_match_advance(es, row, fast, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT_LITERAL: pull from field 1, the element must equal the
   constant in field 2. Row LABEL, slot, const.
*/
KOS_MATCH_BODY(kos_match_next_literal) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_NEXT_LITERAL_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
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
  if (unlikely(kos_match_advance(es, row, fast, &step) < 0))
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
  int fast = op == KOS_OP_MATCH_REST_FAST
      || op == KOS_OP_MATCH_REST_BIND_FAST;
  int bind = op == KOS_OP_MATCH_REST_BIND_VAR
      || op == KOS_OP_MATCH_REST_BIND_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  PyObject *value = NULL;
  PyObject *next = NULL;
  int r = KosMatch_Rest(cursor, KosExtra_IMM(row, 2), &value, &next);
  Py_DECREF(cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  if (bind) {
    if (unlikely(kos_match_store(es, row, 3, fast, value) < 0)) {
      Py_XDECREF(next);
      RESULT_RAISE();
    }
  } else {
    Py_DECREF(value);
  }
  if (next && unlikely(kos_match_store(es, row, 1, fast, next) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_END: the cursor in field 1 must be at its end. Row LABEL,
   slot.
*/
KOS_MATCH_BODY(kos_match_end) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_END_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  int r = KosMatch_End(cursor);
  Py_DECREF(cursor);
  if (! r)
    KOS_MATCH_MISS(row);
  RESULT_NEXT(es);
}


/**
   MATCH_AVAILABLE: the cursor in field 1 must have at least the count
   in field 2 of elements left. Row LABEL, slot, RAW.
*/
KOS_MATCH_BODY(kos_match_available) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_AVAILABLE_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();
  int r = KosMatch_Available(cursor, (Py_ssize_t) KosExtra_IMM(row, 2));
  Py_DECREF(cursor);
  if (! r)
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
   MATCH_NEXT_SAME: pull from field 1; the element must equal the value
   bound in field 2. Row LABEL, slot, slot.
*/
KOS_MATCH_BODY(kos_match_next_same) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_NEXT_SAME_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
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
  if (unlikely(kos_match_advance(es, row, fast, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_NEXT_APPEND: pull from field 1 and append the boxed element to
   the list in field 2. Row LABEL, slot, slot.
*/
KOS_MATCH_BODY(kos_match_next_append) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_NEXT_APPEND_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();

  KosMatchStep step;
  int r = KosMatch_Pull(cursor, &step);
  Py_DECREF(cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);

  PyObject *list = kos_match_load(es, row, 2, fast);
  if (unlikely(! list)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "match append requires a list, not %.200s",
                 Py_TYPE(list)->tp_name);
    Py_DECREF(list);
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  PyObject *boxed = KosMatch_Box(&step);
  int rc = boxed ? PyList_Append(list, boxed) : -1;
  Py_XDECREF(boxed);
  Py_DECREF(list);
  if (unlikely(rc < 0)) {
    KosMatch_StepClear(&step);
    RESULT_RAISE();
  }
  if (unlikely(kos_match_advance(es, row, fast, &step) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_COLLECT: every remaining element of the cursor in field 1,
   boxed from a pair spine, appended to the list in field 2; an
   improper tail is a miss. Row LABEL, slot, slot.
*/
KOS_MATCH_BODY(kos_match_collect) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_COLLECT_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 1, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *list = kos_match_load(es, row, 2, fast);
  if (unlikely(! list)) {
    Py_DECREF(cursor);
    RESULT_RAISE();
  }
  if (unlikely(! PyList_Check(list))) {
    PyErr_Format(PyExc_TypeError,
                 "match collect requires a list, not %.200s",
                 Py_TYPE(list)->tp_name);
    Py_DECREF(list);
    Py_DECREF(cursor);
    RESULT_RAISE();
  }

  PyObject *next = NULL;
  int r = KosMatch_Collect(cursor, list, &next);
  Py_DECREF(list);
  Py_DECREF(cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (! r)
    KOS_MATCH_MISS(row);
  if (next && unlikely(kos_match_store(es, row, 1, fast, next) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_MARK: the position of the cursor in field 0 stored into field
   1. Row slot, slot.
*/
KOS_MATCH_BODY(kos_match_mark) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_MARK_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 0, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *pos = KosMatch_Position(cursor);
  Py_DECREF(cursor);
  if (unlikely(! pos))
    RESULT_RAISE();
  if (unlikely(kos_match_store(es, row, 1, fast, pos) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   MATCH_RESET: the cursor in field 0 moved back to the position in
   field 1. Row slot, slot.
*/
KOS_MATCH_BODY(kos_match_reset) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_RESET_FAST;
  (void) kos_match_delivery_;

  PyObject *cursor = kos_match_load(es, row, 0, fast);
  if (unlikely(! cursor))
    RESULT_RAISE();
  PyObject *saved = kos_match_load(es, row, 1, fast);
  if (unlikely(! saved)) {
    Py_DECREF(cursor);
    RESULT_RAISE();
  }
  PyObject *restored = NULL;
  int r = KosMatch_Reset(cursor, saved, &restored);
  Py_DECREF(saved);
  Py_DECREF(cursor);
  if (unlikely(r < 0))
    RESULT_RAISE();
  if (restored && unlikely(kos_match_store(es, row, 0, fast, restored) < 0))
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
    int fast,
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
  (void) fast;
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
   the start in field 2 and the cursor in field 3; on the NAMED forms
   the start in field 1 and the cursor in field 2, the captures read
   by name. Pushes the captures, or the iteration's value.
*/
KOS_MATCH_BODY(kos_match_captures) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOpCode op = (KosOpCode) KosOp_OP(insn);
  int iteration = op == KOS_OP_MATCH_ITERATION
      || op == KOS_OP_MATCH_ITERATION_NAMED_VAR
      || op == KOS_OP_MATCH_ITERATION_NAMED_FAST;
  int by_name = op != KOS_OP_MATCH_CAPTURES && op != KOS_OP_MATCH_ITERATION;
  int fast = op == KOS_OP_MATCH_CAPTURES || op == KOS_OP_MATCH_ITERATION
      || op == KOS_OP_MATCH_CAPTURES_NAMED_FAST
      || op == KOS_OP_MATCH_ITERATION_NAMED_FAST;
  int start_field = by_name ? 1 : 2;
  (void) kos_match_delivery_;

  /* the slot-run forms name the keys; the NAMED forms carry (keys,
     locals), the locals being the names the captures are read by */
  PyObject *table = KosExtra_OBJ(row, 0);
  PyObject *names = by_name ? PyTuple_GET_ITEM(table, 0) : table;
  PyObject *locals = by_name ? PyTuple_GET_ITEM(table, 1) : table;
  PyObject *start = kos_match_load(es, row, start_field, fast);
  if (unlikely(! start))
    RESULT_RAISE();
  PyObject *cursor = kos_match_load(es, row, start_field + 1, fast);
  if (unlikely(! cursor)) {
    Py_DECREF(start);
    RESULT_RAISE();
  }

  PyObject *buf[KOS_MATCH_GATHER_INLINE];
  PyObject **values = kos_match_gather_values(
      es, locals, by_name, fast, by_name ? 0 : KosExtra_IMM(row, 1), buf);
  if (unlikely(! values)) {
    Py_DECREF(cursor);
    Py_DECREF(start);
    RESULT_RAISE();
  }

  PyObject *out = iteration
      ? KosMatch_Iteration(names, values, start, cursor)
      : KosMatch_Captures(names, values, start, cursor);
  kos_match_release_values(values, PyTuple_GET_SIZE(names), buf);
  Py_DECREF(cursor);
  Py_DECREF(start);
  if (unlikely(! out))
    RESULT_RAISE();

  /* an iteration leaves its slots clear for the next one */
  if (iteration && unlikely(kos_match_clear_values(
          es, locals, by_name, by_name ? 0 : KosExtra_IMM(row, 1)) < 0)) {
    Py_DECREF(out);
    RESULT_RAISE();
  }
  RESULT_STEAL(es, out);
}


// The end.
