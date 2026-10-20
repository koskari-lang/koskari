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

  The bodies two or more MATCH_* handlers tail-call. An instruction
  with a slot field shares one with its VAR and FAST twins, and the
  body reads which it is off the opcode to resolve its slot operands
  by name or by index; MATCH_CAPTURES and MATCH_PASS share another.
  Every other MATCH_* instruction's body is its handler in opcode/.
  Included into eval.c ahead of the opcode handlers. A walk's
  cursors, marks and items lists are stack entries named by depth.
  The steps themselves are the matcher subsystem's (matcher.h,
  matcher/step.c, matcher/cursor.c, matcher/mark.c); a body is the
  interface between the row and the stack.

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


#define KOS_MATCH_BODY(name)                                            \
  static KOS_PRESERVE_NONE KosOpResult name(                            \
      KosEvalState *es, void *insn, PyObject *kos_match_delivery_)

#define KOS_MATCH_MISS(row)  RESULT_JUMP_AT(es, KosExtra_TARGET((row), 0))


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
      && unlikely(KosEvalState_StackSetUnder(es, KosExtra_IMM(row, field), target) < 0))
    return NULL;
  return target;
}


/* === cursors === */


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


/* === marks === */


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


/* === items === */


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


/* === element tests === */


/**
   MATCH_TYPE_VAR and MATCH_TYPE_FAST: TOS, which stays, must be an
   instance of the type in the slot in field 1. Row LABEL, slot.
*/
KOS_MATCH_BODY(kos_match_type) {
  KOS_SYNC_PC(es, insn);
  const KosExtra *row = KosOp_ROW(insn);
  int fast = KosOp_OP(insn) == KOS_OP_MATCH_TYPE_FAST;
  (void) kos_match_delivery_;

  PyObject *elem = KosEvalState_StackPeek(es);
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

  PyObject *elem = KosEvalState_StackPeek(es);
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

  PyObject *obj = KosEvalState_StackPeek(es);
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

  PyObject *obj = KosEvalState_StackPeek(es);
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
