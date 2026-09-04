/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


/**
   koskari/lib/eval/eval_electors.c

   Electors: handlers elected once per site and tail-called on every
   later execution. This file is #included into eval.c after the opcode
   handler macros and before handlers.incl, so a handler can name its
   family's electing function; it is not a standalone translation unit.

   A cache-style elector lives in a KOS_CACHE_ELECTOR slot. The handler
   loads the slot and tail-calls the elected body; the body carries its
   own guard, so the handler's hot path is one load and one indirect
   jump. A guard miss tail-calls the family's miss function, which runs
   the generic body for that execution and counts; at the family's
   limit the slot demotes to the generic body for good, the method
   cache's rule. An empty slot and an ungated site (no slot) take the
   electing function, which classifies the operands once and settles
   the slot.

   Four families share the shape: OBJECT_OPERAND and OBJECT_INPLACE
   read a pair off the stack and replace it with the result;
   FAST_INPLACE reads its accumulator from a fast slot and its operand
   off the stack; FAST_INPLACE_CONST reads the accumulator from a fast
   slot and its operand from the pool. Each has a generic body (the
   former handler out of line), a miss function, one int body per
   operator with an int fast path, and an electing function.

   See design/interpreter/electors.md.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


/* === Elector prototype and slot settlement === */


/* Every elector has the KosThreadFn prototype, as a handler does. It is
   a direct musttail target, so it stays out of line. insn is the site's
   own KosOp; value is the delivery argument, unused by the families
   that read their operands off the stack or a slot. */
#define KOS_ELECTOR(name)                                               \
  static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult el_##name(          \
      KosEvalState *es, void *insn, PyObject *value)


/* Guard misses since the election before a site demotes. */
#define KOS_ELECTOR_MISS_LIMIT 8


/* Populate a slot with the body the family's classification chose. A
   site with no slot only counts as uncached; the body still runs. The
   kind is stored last so a slot never reads as elected before its body
   is in place. */
static inline void kos_elector_elect(KosCacheSlot *slot, KosThreadFn fn) {
  if (! slot) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_UNCACHED);
    return;
  }

  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_MISS);
  slot->u.elector.fn = fn;
  slot->u.elector.misses = 0;
  slot->u.elector.mode = KOS_ELECTOR_ELECTED;
  slot->kind = KOS_CACHE_ELECTOR;
}


/* Count a guard miss at an elected site. At limit the slot demotes:
   the generic body becomes the elected one, so the site runs its
   family's ladder on every execution and never counts again. */
static inline void kos_elector_miss(KosCacheSlot *slot, KosThreadFn generic) {
  uint8_t n;

  if (! slot || slot->kind != KOS_CACHE_ELECTOR)
    return;

  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_MISS);
  n = (uint8_t) (slot->u.elector.misses + 1);
  if (n >= KOS_ELECTOR_MISS_LIMIT) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_BYPASS);
    slot->u.elector.fn = generic;
    slot->u.elector.misses = 0;
    slot->u.elector.mode = KOS_ELECTOR_MEGAMORPHIC;
    return;
  }
  slot->u.elector.misses = n;
}


/* === Shared int-body pieces === */


/* Under the safety net a stack-pair int body refuses a site whose
   operand count is not the two it was elected for. */
#if KOS_EVAL_SAFETYNET
# define KOS_ELECTOR_PAIR_SAFETYNET(insn)                               \
  do {                                                                  \
    if (unlikely(KosOpCount_COUNT(KosOp_ARG(insn)) != 2)) {             \
      PyErr_SetString(PyExc_SystemError,                                \
                      "int elector on an operand site of count != 2");  \
      return EVAL_RAISE();                                              \
    }                                                                   \
  } while (0)
#else
# define KOS_ELECTOR_PAIR_SAFETYNET(insn)  ((void) 0)
#endif


/* Write the result over the left operand and drop the right one. Both
   operands are the stack's references, released only after the
   arithmetic that read them; the drop releases the right one. */
KOS_OP_INLINE void kos_elector_replace2(
    KosSegment **bl,
    PyObject *const *args,
    PyObject *out) {

  PyObject *left = args[0];

  ((PyObject **) args)[0] = out;
  kos_stack_drop1_fast(bl);
  Py_DECREF(left);
}


/* One int body per operator for a family whose two operands are the
   top of the stack: the guard is the two exact-int checks on the
   peeked operands, then the operator's own fast path. A guard miss
   counts toward the demotion; an int the fast path declines (wider
   than a long long, an overflowing result, a negative shift count)
   takes the generic body for this execution without counting, since
   the site is still an int site. int OP= int is value-identical to
   int OP int, so the in-place family shares the fast paths. */
#define KOS_PAIR_INT_ELECTOR(FAMILY, NAME, FAST)                        \
  KOS_ELECTOR(FAMILY##_int_##NAME) {                                    \
    KosSegment **bl = KosEvalState_BLOCK(es);                           \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);            \
    PyObject *out;                                                      \
    int rc;                                                             \
                                                                        \
    KOS_ELECTOR_PAIR_SAFETYNET(insn);                                   \
    if (unlikely(! args))                                               \
      RESULT_RAISE();                                                   \
    if (unlikely(! (PyLong_CheckExact(args[0])                          \
                    && PyLong_CheckExact(args[1]))))                    \
      TAILCALL(el_##FAMILY##_miss, es, insn, value);                    \
                                                                        \
    rc = FAST(args[0], args[1], &out);                                  \
    if (unlikely(rc <= 0)) {                                            \
      if (rc < 0)                                                       \
        RESULT_RAISE();                                                 \
      TAILCALL(el_##FAMILY##_generic, es, insn, value);                 \
    }                                                                   \
                                                                        \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    kos_elector_replace2(bl, args, out);                                \
    RESULT_NEXT(es);                                                    \
  }


/* One int body per operator for a family whose accumulator is a fast
   slot: the guard is the exact-int check on the peeked slot value and
   on the operand the family supplies (RHS, borrowed). The result is
   stolen into the slot after the family's operand release (DROP). A
   slot the peek cannot read (empty, or a ref kind that needs the
   general deref) is a miss; the generic body raises or resolves it. */
#define KOS_SLOT_INT_ELECTOR(FAMILY, NAME, FAST, RHS, DROP)             \
  KOS_ELECTOR(FAMILY##_int_##NAME) {                                    \
    const KosExtra *row = KosOp_ROW(insn);                              \
    size_t idx = (size_t) KosExtra_IMM(row, 1);                         \
    PyObject *ns = KosEvalState_NAMESPACE(es);                          \
    PyObject *acc = kos_ns_fast_peek(ns, idx);                          \
    PyObject *rhs = (RHS);                                              \
    PyObject *out;                                                      \
    int rc;                                                             \
                                                                        \
    if (unlikely(! rhs))                                                \
      RESULT_RAISE();                                                   \
    if (unlikely(! acc || ! PyLong_CheckExact(acc)                      \
                 || ! PyLong_CheckExact(rhs)))                          \
      TAILCALL(el_##FAMILY##_miss, es, insn, value);                    \
                                                                        \
    rc = FAST(acc, rhs, &out);                                          \
    if (unlikely(rc <= 0)) {                                            \
      if (rc < 0)                                                       \
        RESULT_RAISE();                                                 \
      TAILCALL(el_##FAMILY##_generic, es, insn, value);                 \
    }                                                                   \
                                                                        \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    DROP;                                                               \
    if (unlikely(kos_ns_fast_store_steal(ns, idx, out) < 0))            \
      RESULT_RAISE();                                                   \
    RESULT_NEXT(es);                                                    \
  }


/* The nine operators with an int fast path, applied to a body macro. */
#define KOS_INT_ELECTORS(BODY, FAMILY)                                  \
  BODY(FAMILY, add, long_fast_add)                                      \
  BODY(FAMILY, subtract, long_fast_subtract)                            \
  BODY(FAMILY, multiply, long_fast_multiply)                            \
  BODY(FAMILY, modulo, long_fast_modulo)                                \
  BODY(FAMILY, bit_and, long_fast_bit_and)                              \
  BODY(FAMILY, bit_or, long_fast_bit_or)                                \
  BODY(FAMILY, bit_xor, long_fast_bit_xor)                              \
  BODY(FAMILY, bit_left, long_fast_bit_left)                            \
  BODY(FAMILY, bit_right, long_fast_bit_right)

/* A family's table of int bodies by operator; NULL where the operator
   has no int fast path (divide, floor-divide, matrix multiply, power). */
#define KOS_INT_ELECTOR_TABLE(FAMILY)                                   \
  static const KosThreadFn kos_##FAMILY##_int_bodies[KOS_OPERAND_COUNT] = { \
    [KOS_OPERAND_ADD] = el_##FAMILY##_int_add,                          \
    [KOS_OPERAND_SUBTRACT] = el_##FAMILY##_int_subtract,                \
    [KOS_OPERAND_MULTIPLY] = el_##FAMILY##_int_multiply,                \
    [KOS_OPERAND_MODULO] = el_##FAMILY##_int_modulo,                    \
    [KOS_OPERAND_BIT_AND] = el_##FAMILY##_int_bit_and,                  \
    [KOS_OPERAND_BIT_OR] = el_##FAMILY##_int_bit_or,                    \
    [KOS_OPERAND_BIT_XOR] = el_##FAMILY##_int_bit_xor,                  \
    [KOS_OPERAND_BIT_LEFT] = el_##FAMILY##_int_bit_left,                \
    [KOS_OPERAND_BIT_RIGHT] = el_##FAMILY##_int_bit_right,              \
  };


/* The int body a stack-pair site elects: the operator's entry in the
   family's table when the site is binary and its two operands are
   exact ints, else NULL. */
static inline KosThreadFn kos_elector_pair_body(
    KosEvalState *es,
    uint16_t arg,
    const KosThreadFn *table) {

  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  KosSegment *s = *KosEvalState_BLOCK(es);

  if (KosOpCount_COUNT(arg) == 2 && op < KOS_OPERAND_COUNT
      && s && s->len >= 2) {
    PyObject *const *args = kos_stack_tail_ptr_fast(s, 2);
    if (PyLong_CheckExact(args[0]) && PyLong_CheckExact(args[1]))
      return table[op];
  }
  return NULL;
}


/* The int body a fast-slot site elects: the operator's entry in the
   family's table when the peeked slot value and the operand are exact
   ints, else NULL. */
static inline KosThreadFn kos_elector_slot_body(
    KosEvalState *es,
    KosOperand op,
    size_t idx,
    PyObject *rhs,
    const KosThreadFn *table) {

  PyObject *acc = kos_ns_fast_peek(KosEvalState_NAMESPACE(es), idx);

  if (op < KOS_OPERAND_COUNT && acc && rhs
      && PyLong_CheckExact(acc) && PyLong_CheckExact(rhs))
    return table[op];
  return NULL;
}


/* === OBJECT_OPERAND family === */


/**
   The generic body: OBJECT_OPERAND as it stands without an election,
   the operator switch and the reduce out of line.
*/
KOS_ELECTOR(operand_generic) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  if (unlikely(kos_object_operand(es, op, count) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   A type-guard miss at an elected int site: count it toward the
   demotion and run the generic body for this execution.
*/
KOS_ELECTOR(operand_miss) {
  kos_elector_miss(KosOp_SLOT(insn), el_operand_generic);
  TAILCALL(el_operand_generic, es, insn, value);
}


KOS_INT_ELECTORS(KOS_PAIR_INT_ELECTOR, operand)
KOS_INT_ELECTOR_TABLE(operand)


/**
   The electing function, reached from an empty slot or an ungated
   site: classify the operands once and settle the slot. An exact-int
   pair at a binary site elects the operator's int body; anything else
   elects the generic body, which never re-elects.
*/
KOS_ELECTOR(operand_elect) {
  KosThreadFn fn = kos_elector_pair_body(
      es, KosOp_ARG(insn), kos_operand_int_bodies);

  if (! fn)
    fn = el_operand_generic;
  kos_elector_elect(KosOp_SLOT(insn), fn);
  TAILCALL(fn, es, insn, value);
}


/* === OBJECT_INPLACE family === */


/**
   The generic body: OBJECT_INPLACE as it stands without an election,
   the in-place reduce out of line.
*/
KOS_ELECTOR(inplace_generic) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  if (unlikely(kos_object_inplace(es, op, count) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


KOS_ELECTOR(inplace_miss) {
  kos_elector_miss(KosOp_SLOT(insn), el_inplace_generic);
  TAILCALL(el_inplace_generic, es, insn, value);
}


KOS_INT_ELECTORS(KOS_PAIR_INT_ELECTOR, inplace)
KOS_INT_ELECTOR_TABLE(inplace)


KOS_ELECTOR(inplace_elect) {
  KosThreadFn fn = kos_elector_pair_body(
      es, KosOp_ARG(insn), kos_inplace_int_bodies);

  if (! fn)
    fn = el_inplace_generic;
  kos_elector_elect(KosOp_SLOT(insn), fn);
  TAILCALL(fn, es, insn, value);
}


/* === FAST_INPLACE family === */


/**
   The generic body: FAST_INPLACE as it stands without an election,
   slot = slot OP top-of-stack through the in-place reduce.
*/
KOS_ELECTOR(fast_inplace_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  size_t idx = (size_t) KosExtra_IMM(row, 1);

  if (unlikely(kos_fast_inplace_operand(es, op, idx) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


KOS_ELECTOR(fast_inplace_miss) {
  kos_elector_miss(KosOp_SLOT(insn), el_fast_inplace_generic);
  TAILCALL(el_fast_inplace_generic, es, insn, value);
}


/* The operand is the top of the stack, borrowed for the arithmetic and
   dropped before the store. */
#define KOS_FAST_INPLACE_INT_ELECTOR(FAMILY, NAME, FAST)                \
  KOS_SLOT_INT_ELECTOR(                                                 \
      FAMILY, NAME, FAST,                                               \
      kos_stack_peek_top(es),                                           \
      kos_stack_drop1_fast(KosEvalState_BLOCK(es)))

/* The top of the stack, borrowed, or NULL with an error set. */
KOS_OP_INLINE PyObject *kos_stack_peek_top(KosEvalState *es) {
  PyObject *const *top = kos_stack_tail_ptr_fast(*KosEvalState_BLOCK(es), 1);
  return top ? top[0] : NULL;
}

KOS_INT_ELECTORS(KOS_FAST_INPLACE_INT_ELECTOR, fast_inplace)
KOS_INT_ELECTOR_TABLE(fast_inplace)


KOS_ELECTOR(fast_inplace_elect) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  size_t idx = (size_t) KosExtra_IMM(row, 1);
  KosSegment *s = *KosEvalState_BLOCK(es);
  PyObject *rhs = (s && s->len >= 1) ? kos_stack_tail_ptr_fast(s, 1)[0] : NULL;
  KosThreadFn fn = kos_elector_slot_body(
      es, op, idx, rhs, kos_fast_inplace_int_bodies);

  if (! fn)
    fn = el_fast_inplace_generic;
  kos_elector_elect(KosOp_SLOT(insn), fn);
  TAILCALL(fn, es, insn, value);
}


/* === FAST_INPLACE_CONST family === */


/**
   The generic body: FAST_INPLACE_CONST as it stands without an
   election, slot = slot OP constant through the in-place reduce.
*/
KOS_ELECTOR(fast_inplace_const_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosEvalState_NAMESPACE(es);
  PyObject *operand = KosExtra_OBJ(row, 2);   /* borrowed */
  size_t idx = (size_t) KosExtra_IMM(row, 1);
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  PyObject *result;

  if (unlikely(! obj))
    RESULT_RAISE();

  {
    PyObject *pair[2] = { obj, operand };
    result = kos_inplace_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  }
  Py_DECREF(obj);
  if (unlikely(! result))
    RESULT_RAISE();

  if (unlikely(kos_ns_fast_store_steal(ns, idx, result) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


KOS_ELECTOR(fast_inplace_const_miss) {
  kos_elector_miss(KosOp_SLOT(insn), el_fast_inplace_const_generic);
  TAILCALL(el_fast_inplace_const_generic, es, insn, value);
}


/* The operand is the pool constant on the row, borrowed; nothing to
   drop. Its type was checked at the election and cannot change. */
#define KOS_FAST_INPLACE_CONST_INT_ELECTOR(FAMILY, NAME, FAST)          \
  KOS_SLOT_INT_ELECTOR(                                                 \
      FAMILY, NAME, FAST,                                               \
      KosExtra_OBJ(row, 2),                                             \
      ((void) 0))

KOS_INT_ELECTORS(KOS_FAST_INPLACE_CONST_INT_ELECTOR, fast_inplace_const)
KOS_INT_ELECTOR_TABLE(fast_inplace_const)


KOS_ELECTOR(fast_inplace_const_elect) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosExtra_IMM(row, 0);
  size_t idx = (size_t) KosExtra_IMM(row, 1);
  KosThreadFn fn = kos_elector_slot_body(
      es, op, idx, KosExtra_OBJ(row, 2), kos_fast_inplace_const_int_bodies);

  if (! fn)
    fn = el_fast_inplace_const_generic;
  kos_elector_elect(KosOp_SLOT(insn), fn);
  TAILCALL(fn, es, insn, value);
}


#undef KOS_FAST_INPLACE_CONST_INT_ELECTOR
#undef KOS_FAST_INPLACE_INT_ELECTOR
#undef KOS_INT_ELECTOR_TABLE
#undef KOS_INT_ELECTORS
#undef KOS_SLOT_INT_ELECTOR
#undef KOS_PAIR_INT_ELECTOR


// The end.
