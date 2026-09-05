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
   jump. A guard miss tail-calls the shared miss function, which runs
   the site's generic body for that execution and counts; at the limit
   the slot demotes to the generic body, the method cache's rule. The
   generic body counts its own executions and empties the slot every
   256, so a demoted site, or one whose first operands were not its
   steady type, re-elects on a bounded interval. An empty slot and an
   ungated site (no slot) take the family's electing function, which
   classifies the operands once and settles the slot.

   Four families share the shape: OBJECT_OPERAND and OBJECT_INPLACE
   read a pair off the stack and replace it with the result, and share
   their int bodies; FAST_INPLACE reads its accumulator from a fast slot
   and its operand off the stack; FAST_INPLACE_CONST reads the
   accumulator from a fast slot and its operand from the pool. Each has
   a generic body (the former handler out of line), one int body per
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

#define KOS_ELECTOR_DECL(name)                                          \
  static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult el_##name(          \
      KosEvalState *es, void *insn, PyObject *value);


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
   family's ladder on every execution until the generic body's own
   count empties the slot for a re-election. */
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


/* One execution of a generic body: the slow path counts in the byte a
   specialised body would use for misses, and when it wraps the slot
   empties, so the next execution classifies again. */
static inline void kos_elector_generic_tick(KosCacheSlot *slot) {
  if (slot && slot->kind == KOS_CACHE_ELECTOR
      && ++slot->u.elector.misses == 0)
    slot->kind = KOS_CACHE_EMPTY;
}


/* === The families' generic bodies === */


KOS_ELECTOR_DECL(operand_generic)
KOS_ELECTOR_DECL(inplace_generic)
KOS_ELECTOR_DECL(fast_inplace_generic)
KOS_ELECTOR_DECL(fast_inplace_const_generic)


/* The generic body of an elector site, by its opcode. Read on the
   miss and decline paths only. */
static inline KosThreadFn kos_elector_generic_of(void *insn) {
  switch (KosOp_OP(insn)) {
    case KOS_OP_OBJECT_OPERAND:
      return el_operand_generic;
    case KOS_OP_OBJECT_INPLACE:
      return el_inplace_generic;
    case KOS_OP_FAST_INPLACE:
      return el_fast_inplace_generic;
    default:
      return el_fast_inplace_const_generic;
  }
}


/**
   OBJECT_OPERAND without an election: the operator switch and the
   reduce out of line.
*/
KOS_ELECTOR(operand_generic) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(kos_object_operand(es, op, count) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   OBJECT_INPLACE without an election: the in-place reduce out of line.
*/
KOS_ELECTOR(inplace_generic) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(kos_object_inplace(es, op, count) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   FAST_INPLACE without an election: slot = slot OP top-of-stack through
   the in-place reduce.
*/
KOS_ELECTOR(fast_inplace_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  size_t idx = (size_t) KosExtra_IMM(row, 1);

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(kos_fast_inplace_operand(es, op, idx) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   FAST_INPLACE_CONST without an election: slot = slot OP constant
   through the in-place reduce.
*/
KOS_ELECTOR(fast_inplace_const_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosExtra_IMM(row, 0);
  size_t idx = (size_t) KosExtra_IMM(row, 1);

  KOS_OP_REQUIRE_NS(KosEvalState_NAMESPACE(es));
  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(kos_fast_inplace_apply(es, op, idx, KosExtra_OBJ(row, 2)) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/**
   The shared miss function: a type-guard miss at an elected int site
   counts toward the demotion and runs the site's generic body for
   this execution.
*/
KOS_ELECTOR(miss) {
  KosThreadFn generic = kos_elector_generic_of(insn);

  kos_elector_miss(KosOp_SLOT(insn), generic);
  TAILCALL(generic, es, insn, value);
}


/* === Int bodies === */


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


/* The tail of every int body once both operands are C values: the
   operator's arithmetic, a decline (overflow, a negative shift, a zero
   divisor) taking the generic body without counting since the site is
   still an int site, an allocation failure raising. */
#define KOS_ELECTOR_INT_RESULT(FAST_LL, a, b, out)                      \
  do {                                                                  \
    int kos_ir_rc_ = FAST_LL((a), (b), &(out));                         \
    if (unlikely(kos_ir_rc_ <= 0)) {                                    \
      if (kos_ir_rc_ < 0)                                               \
        RESULT_RAISE();                                                 \
      TAILCALL(kos_elector_generic_of(insn), es, insn, value);          \
    }                                                                   \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
  } while (0)


/* One int body per operator for the two families whose operands are
   the top pair of the stack, OBJECT_OPERAND and OBJECT_INPLACE (int OP=
   int is value-identical to int OP int). The guard is the two exact-int
   checks on the peeked pair; a miss counts. An int wider than two
   digits and the long long range takes the generic body without
   counting. The result replaces the pair. */
#define KOS_PAIR_INT_ELECTOR(NAME, FAST_LL)                             \
  KOS_ELECTOR(pair_int_##NAME) {                                        \
    KosSegment **bl = KosEvalState_BLOCK(es);                           \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);            \
    long long a, b;                                                     \
    PyObject *out;                                                      \
                                                                        \
    KOS_ELECTOR_PAIR_SAFETYNET(insn);                                   \
    if (unlikely(! args))                                               \
      RESULT_RAISE();                                                   \
    if (unlikely(! (PyLong_CheckExact(args[0])                          \
                    && PyLong_CheckExact(args[1]))))                    \
      TAILCALL(el_miss, es, insn, value);                               \
    if (unlikely(! kos_long_as_ll(args[0], &a)                          \
                 || ! kos_long_as_ll(args[1], &b)))                     \
      TAILCALL(kos_elector_generic_of(insn), es, insn, value);          \
                                                                        \
    KOS_ELECTOR_INT_RESULT(FAST_LL, a, b, out);                         \
    kos_stack_replace2_fast(bl, out);                                   \
    RESULT_NEXT(es);                                                    \
  }


/* One int body per operator for a family whose accumulator is a fast
   slot: the guard is the exact-int check on the peeked slot value and
   on the operand the family supplies (RHS, borrowed). The result is
   stolen into the slot after the family's operand release (DROP). A
   slot the peek cannot read (empty, or a ref kind that needs the
   general deref) is a miss; the generic body raises or resolves it. */
#define KOS_SLOT_INT_ELECTOR(FAMILY, NAME, FAST_LL, RHS, DROP)          \
  KOS_ELECTOR(FAMILY##_int_##NAME) {                                    \
    const KosExtra *row = KosOp_ROW(insn);                              \
    size_t idx = (size_t) KosExtra_IMM(row, 1);                         \
    PyObject *ns = KosEvalState_NAMESPACE(es);                          \
    PyObject *acc;                                                      \
    PyObject *rhs;                                                      \
    long long a, b;                                                     \
    PyObject *out;                                                      \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    acc = kos_ns_fast_peek(ns, idx);                                    \
    rhs = (RHS);                                                        \
    if (unlikely(! rhs))                                                \
      RESULT_RAISE();                                                   \
    if (unlikely(! acc || ! PyLong_CheckExact(acc)                      \
                 || ! PyLong_CheckExact(rhs)))                          \
      TAILCALL(el_miss, es, insn, value);                               \
    if (unlikely(! kos_long_as_ll(acc, &a) || ! kos_long_as_ll(rhs, &b))) \
      TAILCALL(kos_elector_generic_of(insn), es, insn, value);          \
                                                                        \
    KOS_ELECTOR_INT_RESULT(FAST_LL, a, b, out);                         \
    DROP;                                                               \
    if (unlikely(kos_ns_fast_store_steal(ns, idx, out) < 0))            \
      RESULT_RAISE();                                                   \
    RESULT_NEXT(es);                                                    \
  }


/* The twelve operators with an int fast path, applied to a body macro
   that takes (family, name, arithmetic). */
#define KOS_INT_ELECTORS(BODY, FAMILY)                                  \
  BODY(FAMILY, add, long_fast_add_ll)                                   \
  BODY(FAMILY, subtract, long_fast_subtract_ll)                         \
  BODY(FAMILY, multiply, long_fast_multiply_ll)                         \
  BODY(FAMILY, modulo, long_fast_modulo_ll)                             \
  BODY(FAMILY, bit_and, long_fast_bit_and_ll)                           \
  BODY(FAMILY, bit_or, long_fast_bit_or_ll)                             \
  BODY(FAMILY, bit_xor, long_fast_bit_xor_ll)                           \
  BODY(FAMILY, bit_left, long_fast_bit_left_ll)                         \
  BODY(FAMILY, bit_right, long_fast_bit_right_ll)                       \
  BODY(FAMILY, floor_divide, long_fast_floor_divide_ll)                 \
  BODY(FAMILY, true_divide, long_fast_true_divide_ll)                   \
  BODY(FAMILY, power, long_fast_power_ll)

/* A family's table of int bodies by operator; NULL where the operator
   has no int fast path (matrix multiply). */
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
    [KOS_OPERAND_FLOOR_DIVIDE] = el_##FAMILY##_int_floor_divide,        \
    [KOS_OPERAND_TRUE_DIVIDE] = el_##FAMILY##_int_true_divide,          \
    [KOS_OPERAND_POWER] = el_##FAMILY##_int_power,                      \
  };


/* The pair bodies take no family: the macro's family slot names them. */
#define KOS_PAIR_INT_BODY(FAMILY, NAME, FAST_LL) KOS_PAIR_INT_ELECTOR(NAME, FAST_LL)

KOS_INT_ELECTORS(KOS_PAIR_INT_BODY, pair)
KOS_INT_ELECTOR_TABLE(pair)


/* The top of the stack, borrowed, or NULL with an error set. */
KOS_OP_INLINE PyObject *kos_stack_peek_top(KosEvalState *es) {
  PyObject *const *top = kos_stack_tail_ptr_fast(*KosEvalState_BLOCK(es), 1);
  return top ? top[0] : NULL;
}

/* FAST_INPLACE: the operand is the top of the stack, borrowed for the
   arithmetic and dropped before the store. */
#define KOS_FAST_INPLACE_INT_BODY(FAMILY, NAME, FAST_LL)                \
  KOS_SLOT_INT_ELECTOR(                                                 \
      FAMILY, NAME, FAST_LL,                                            \
      kos_stack_peek_top(es),                                           \
      kos_stack_drop1_fast(KosEvalState_BLOCK(es)))

KOS_INT_ELECTORS(KOS_FAST_INPLACE_INT_BODY, fast_inplace)
KOS_INT_ELECTOR_TABLE(fast_inplace)


/* FAST_INPLACE_CONST: the operand is the pool constant on the row,
   borrowed; nothing to drop. Its type was checked at the election and
   cannot change. */
#define KOS_FAST_INPLACE_CONST_INT_BODY(FAMILY, NAME, FAST_LL)          \
  KOS_SLOT_INT_ELECTOR(                                                 \
      FAMILY, NAME, FAST_LL,                                            \
      KosExtra_OBJ(row, 2),                                             \
      ((void) 0))

KOS_INT_ELECTORS(KOS_FAST_INPLACE_CONST_INT_BODY, fast_inplace_const)
KOS_INT_ELECTOR_TABLE(fast_inplace_const)


/* === Election === */


/* The int body a site elects for two borrowed operands: the operator's
   entry in the family's table when both are exact ints, else NULL. */
static inline KosThreadFn kos_elector_int_body(
    KosOperand op,
    PyObject *l,
    PyObject *r,
    const KosThreadFn *table) {

  if (op < KOS_OPERAND_COUNT && l && r
      && PyLong_CheckExact(l) && PyLong_CheckExact(r))
    return table[op];
  return NULL;
}


/* The classification of a stack-pair site: binary, with the pair on
   the stack. */
static inline KosThreadFn kos_elector_pair_classify(KosEvalState *es, void *insn) {
  uint16_t arg = KosOp_ARG(insn);
  KosSegment *s = *KosEvalState_BLOCK(es);
  PyObject *const *args;

  if (KosOpCount_COUNT(arg) != 2 || ! s || s->len < 2)
    return NULL;
  args = kos_stack_tail_ptr_fast(s, 2);
  return kos_elector_int_body(
      (KosOperand) KosOpCount_TYPE(arg), args[0], args[1],
      kos_pair_int_bodies);
}


/* The classification of a FAST_INPLACE site: the peeked slot value and
   the top of the stack. */
static inline KosThreadFn kos_elector_fast_inplace_classify(
    KosEvalState *es,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  KosSegment *s = *KosEvalState_BLOCK(es);
  PyObject *acc = kos_ns_fast_peek(
      KosEvalState_NAMESPACE(es), (size_t) KosExtra_IMM(row, 1));
  PyObject *rhs = (s && s->len >= 1) ? kos_stack_tail_ptr_fast(s, 1)[0] : NULL;

  return kos_elector_int_body(
      (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0)), acc, rhs,
      kos_fast_inplace_int_bodies);
}


/* The classification of a FAST_INPLACE_CONST site: the peeked slot
   value and the pool constant. */
static inline KosThreadFn kos_elector_fast_inplace_const_classify(
    KosEvalState *es,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *acc = kos_ns_fast_peek(
      KosEvalState_NAMESPACE(es), (size_t) KosExtra_IMM(row, 1));

  return kos_elector_int_body(
      (KosOperand) KosExtra_IMM(row, 0), acc, KosExtra_OBJ(row, 2),
      kos_fast_inplace_const_int_bodies);
}


/* A family's electing function, reached from an empty slot or an
   ungated site: classify the operands once, settle the slot with the
   int body or the generic body, and dispatch. */
#define KOS_ELECTOR_ELECT(FAMILY, CLASSIFY)                             \
  KOS_ELECTOR(FAMILY##_elect) {                                         \
    KosThreadFn fn = CLASSIFY(es, insn);                                \
                                                                        \
    if (! fn)                                                           \
      fn = el_##FAMILY##_generic;                                       \
    kos_elector_elect(KosOp_SLOT(insn), fn);                            \
    TAILCALL(fn, es, insn, value);                                      \
  }

KOS_ELECTOR_ELECT(operand, kos_elector_pair_classify)
KOS_ELECTOR_ELECT(inplace, kos_elector_pair_classify)
KOS_ELECTOR_ELECT(fast_inplace, kos_elector_fast_inplace_classify)
KOS_ELECTOR_ELECT(fast_inplace_const, kos_elector_fast_inplace_const_classify)


#undef KOS_ELECTOR_ELECT
#undef KOS_FAST_INPLACE_CONST_INT_BODY
#undef KOS_FAST_INPLACE_INT_BODY
#undef KOS_PAIR_INT_BODY
#undef KOS_INT_ELECTOR_TABLE
#undef KOS_INT_ELECTORS
#undef KOS_SLOT_INT_ELECTOR
#undef KOS_PAIR_INT_ELECTOR
#undef KOS_ELECTOR_INT_RESULT
#undef KOS_ELECTOR_DECL


// The end.
