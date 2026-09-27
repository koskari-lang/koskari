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

   The arithmetic families share the shape: OBJECT_OPERAND and
   OBJECT_INPLACE read a pair off the stack and replace it with the
   result, and share their bodies; FAST_INPLACE reads its
   accumulator from a fast slot and its operand off the stack;
   FAST_INPLACE_CONST reads the accumulator from a fast slot and its
   operand from the pool. Each has a generic body (the former handler
   out of line), one body per operator and operand kind (int, float)
   with a fast path of that kind, a str body for +, and an electing
   function; the two stack families also elect, on a run of three or
   more operands of one kind, a fold body per operator and kind that
   folds the run in C and materialises once, and a join body on a run
   of strs. The compare families have one body per kind each, the
   selector being a site constant the body reads: CHECK_COMPARE takes
   the stack pair and jumps, OBJECT_COMPARE takes
   it and pushes the bool. CHECK_COMPARE_FAST keeps its inline int arm
   in the handler: measured with a slot it lost what the hop cost.
   FAST_ATTR_INPLACE reads its accumulator through the site's field
   cache, which is the first of its two slots; the elector is the
   second, and its bodies guard through the field slot on every
   execution and swap the result into the same location.
   The item families elect through the helpers in eval_item.c.

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
  slot->u.elector.ticks = 0;
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
    slot->u.elector.ticks = 0;
    slot->u.elector.mode = KOS_ELECTOR_MEGAMORPHIC;
    return;
  }
  slot->u.elector.misses = n;
}


/* One execution of a generic body, reached by demotion or by a decline
   from an elected body: the slow path counts in its own byte, and when
   it wraps the slot empties, so the next execution classifies again.
   A decline is not a guard miss and never moves the site toward
   demotion. */
static inline void kos_elector_generic_tick(KosCacheSlot *slot) {
  if (slot && slot->kind == KOS_CACHE_ELECTOR
      && ++slot->u.elector.ticks == 0)
    slot->kind = KOS_CACHE_EMPTY;
}


/* === The families' generic bodies === */


KOS_ELECTOR_DECL(operand_generic)
KOS_ELECTOR_DECL(inplace_generic)
KOS_ELECTOR_DECL(fast_inplace_generic)
KOS_ELECTOR_DECL(fast_inplace_const_generic)
KOS_ELECTOR_DECL(operand_const_generic)
KOS_ELECTOR_DECL(fast_operand_const_generic)
KOS_ELECTOR_DECL(check_compare_generic)
KOS_ELECTOR_DECL(object_compare_generic)
KOS_ELECTOR_DECL(fast_attr_inplace_generic)


/* The elector slot of a site: the site's slot, except FAST_ATTR_INPLACE,
   whose first slot is its field cache and whose elector is the second.
   Read on the miss path only; the handlers and bodies know their own. */
static inline KosCacheSlot *kos_elector_slot_of(void *insn) {
  KosCacheSlot *slot = KosOp_SLOT(insn);

  if (slot && KosOp_OP(insn) == KOS_OP_FAST_ATTR_INPLACE)
    return slot + 1;
  return slot;
}


/* The generic body of an elector site, by its opcode. Read on the
   miss and decline paths only, and kept out of line so the switch
   does not grow every body. */
static KOS_NOINLINE KosThreadFn kos_elector_generic_of(void *insn) {
  switch (KosOp_OP(insn)) {
    case KOS_OP_OBJECT_OPERAND:
      return el_operand_generic;
    case KOS_OP_OBJECT_INPLACE:
      return el_inplace_generic;
    case KOS_OP_FAST_INPLACE:
      return el_fast_inplace_generic;
    case KOS_OP_FAST_INPLACE_CONST:
      return el_fast_inplace_const_generic;
    case KOS_OP_OBJECT_OPERAND_CONST:
      return el_operand_const_generic;
    case KOS_OP_FAST_OPERAND_CONST:
      return el_fast_operand_const_generic;
    case KOS_OP_CHECK_COMPARE:
      return el_check_compare_generic;
    case KOS_OP_FAST_ATTR_INPLACE:
      return el_fast_attr_inplace_generic;
    default:
      return el_object_compare_generic;
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
/**
   OBJECT_OPERAND_CONST without an election: the stack top and the
   row's constant through the operator's reduce, the result replacing
   the top.
*/
KOS_ELECTOR(operand_const_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *const *top = kos_stack_tail_ptr_fast(*bl, 1);
  PyObject *pair[2];
  PyObject *result;

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(! top))
    RESULT_RAISE();
  pair[0] = top[0];
  pair[1] = KosExtra_OBJ(row, 1);
  result = kos_operand_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  if (unlikely(! result))
    RESULT_RAISE();
  kos_stack_drop1_fast(bl);
  RESULT_STEAL(es, result);
}


/**
   FAST_OPERAND_CONST without an election: the slot's value (derefed,
   so an empty slot raises and a ref runs its own deref) and the row's
   constant through the operator's reduce, the result pushed.
*/
KOS_ELECTOR(fast_operand_const_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosEvalState_NAMESPACE(es);
  PyObject *pair[2];
  PyObject *result;

  KOS_OP_REQUIRE_NS(ns);
  kos_elector_generic_tick(KosOp_SLOT(insn));
  pair[0] = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 1));
  if (unlikely(! pair[0]))
    RESULT_RAISE();
  pair[1] = KosExtra_OBJ(row, 2);
  result = kos_operand_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  Py_DECREF(pair[0]);
  if (unlikely(! result))
    RESULT_RAISE();
  RESULT_STEAL(es, result);
}


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
   The shared miss function: a type-guard miss at an elected site
   counts toward the demotion and runs the site's generic body for
   this execution.
*/
KOS_ELECTOR(miss) {
  KosThreadFn generic = kos_elector_generic_of(insn);

  kos_elector_miss(kos_elector_slot_of(insn), generic);
  TAILCALL(generic, es, insn, value);
}


/* === Numeric bodies === */


/* Under the safety net a stack-pair body refuses a site whose operand
   count is not the two it was elected for. */
#if KOS_EVAL_SAFETYNET
# define KOS_ELECTOR_PAIR_SAFETYNET(insn)                               \
  do {                                                                  \
    if (unlikely(KosOpCount_COUNT(KosOp_ARG(insn)) != 2)) {             \
      PyErr_SetString(PyExc_SystemError,                                \
                      "pair elector on an operand site of count != 2"); \
      return EVAL_RAISE();                                              \
    }                                                                   \
  } while (0)
#else
# define KOS_ELECTOR_PAIR_SAFETYNET(insn)  ((void) 0)
#endif


/* The two operand kinds a numeric body is written for, INT and FLOAT.
   Each kind names the C type of its extracted operands, the check on
   one borrowed object and the guard on two, the extraction of one or
   two C values (an int wider than the long long range declines to
   DECLINE, the family's generic body, without counting: the site is
   still a site of that type), the compare over two C values, the
   materialisation of a C result into an object, and an operator's
   materialising fast path and its pure step by name. A body macro
   takes the kind in both spellings, KIND for these and kind for its
   name. */
#define KOS_INT_TYPE            long long
#define KOS_INT_VARS            long long a, b
#define KOS_INT_CHECK(v)        PyLong_CheckExact(v)
#define KOS_INT_GUARD(l, r)     (KOS_INT_CHECK(l) && KOS_INT_CHECK(r))
#define KOS_INT_EXTRACT_ONE(v, into, DECLINE)                           \
  do {                                                                  \
    if (unlikely(! kos_long_as_ll((v), &(into))))                       \
      TAILCALL(DECLINE, es, insn, value);                               \
  } while (0)
#define KOS_INT_EXTRACT(l, r, DECLINE)                                  \
  do {                                                                  \
    if (unlikely(! kos_long_as_ll((l), &a) || ! kos_long_as_ll((r), &b))) \
      TAILCALL(DECLINE, es, insn, value);                               \
  } while (0)
#define KOS_INT_COMPARE         kos_ll_compare
#define KOS_INT_MATERIALISE     kos_long_fast_result
#define KOS_INT_FAST(NAME)      long_fast_##NAME##_ll
#define KOS_INT_STEP(NAME)      kos_ll_##NAME

#define KOS_FLOAT_TYPE          double
#define KOS_FLOAT_VARS          double a, b
#define KOS_FLOAT_CHECK(v)      PyFloat_CheckExact(v)
#define KOS_FLOAT_GUARD(l, r)   (KOS_FLOAT_CHECK(l) && KOS_FLOAT_CHECK(r))
#define KOS_FLOAT_EXTRACT_ONE(v, into, DECLINE)                         \
  do {                                                                  \
    (into) = PyFloat_AS_DOUBLE(v);                                      \
  } while (0)
#define KOS_FLOAT_EXTRACT(l, r, DECLINE)                                \
  do {                                                                  \
    a = PyFloat_AS_DOUBLE(l);                                           \
    b = PyFloat_AS_DOUBLE(r);                                           \
  } while (0)
#define KOS_FLOAT_COMPARE       kos_double_compare
#define KOS_FLOAT_MATERIALISE   kos_float_fast_result
#define KOS_FLOAT_FAST(NAME)    float_fast_##NAME##_d
#define KOS_FLOAT_STEP(NAME)    kos_d_##NAME


/* The tail of every numeric body once both operands are C values: the
   operator's arithmetic, a decline (overflow, a negative shift, a zero
   divisor) taking the generic body without counting, an allocation
   failure raising. */
#define KOS_ELECTOR_RESULT_HELD(FAST, a, b, reuse, out, RELEASE)        \
  do {                                                                  \
    int kos_ir_rc_ = FAST((a), (b), (reuse), &(out));                   \
    if (unlikely(kos_ir_rc_ <= 0)) {                                    \
      RELEASE;                                                          \
      if (kos_ir_rc_ < 0)                                               \
        RESULT_RAISE();                                                 \
      TAILCALL(kos_elector_generic_of(insn), es, insn, value);          \
    }                                                                   \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
  } while (0)

/* The common form: nothing is held across the arithmetic. RELEASE in
   the held form runs before a decline or a raise leaves the body. */
#define KOS_ELECTOR_RESULT(FAST, a, b, reuse, out)                      \
  KOS_ELECTOR_RESULT_HELD(FAST, a, b, reuse, out, ((void) 0))


/* One body per operator and kind for the two families whose operands
   are the top pair of the stack, OBJECT_OPERAND and OBJECT_INPLACE
   (int OP= int is value-identical to int OP int). The guard is the
   kind's check on the peeked pair; a miss counts. The left operand is
   offered as the result's home: when the stack is its only owner the
   result is written into it and the right operand alone is dropped;
   else the result replaces the pair. */
#define KOS_PAIR_ELECTOR(FAMILY, KIND, kind, NAME, OPERAND)             \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    KosSegment **bl = KosEvalState_BLOCK(es);                           \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);            \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    KOS_ELECTOR_PAIR_SAFETYNET(insn);                                   \
    if (unlikely(! args))                                               \
      RESULT_RAISE();                                                   \
    if (unlikely(! KOS_##KIND##_GUARD(args[0], args[1])))               \
      TAILCALL(el_miss, es, insn, value);                               \
    KOS_##KIND##_EXTRACT(args[0], args[1], kos_elector_generic_of(insn)); \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, args[0], out);    \
    if (out == args[0])                                                 \
      kos_stack_drop1_fast(bl);                                         \
    else                                                                \
      kos_stack_replace2_fast(bl, out);                                 \
    RESULT_NEXT(es);                                                    \
  }


/* One body per operator and kind for a family whose accumulator is a
   fast slot: the guard is the kind's check on the peeked slot value
   and, when RHS_GUARDED, on the operand the family supplies (RHS,
   borrowed; a pool constant's type was fixed at the election). The
   accumulator is offered as the result's home when the slot is raw
   and its only owner: the result is written into it and no store
   follows. A ref slot's value is never written: its store may refuse
   (a constant ref), so the result is stolen into the slot through the
   ref, as it is into a raw slot the value is shared with. Either way
   the family's operand release (DROP) runs first. A slot the peek
   cannot read (empty, or a ref kind that needs the general deref) is
   a miss; the generic body raises or resolves it. */
#define KOS_SLOT_ELECTOR(FAMILY, KIND, kind, NAME, RHS, RHS_GUARDED, DROP) \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    size_t idx = (size_t) KosExtra_IMM(row, 1);                         \
    PyObject *ns = KosEvalState_NAMESPACE(es);                          \
    PyObject *acc;                                                      \
    PyObject *rhs;                                                      \
    int raw;                                                            \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    acc = kos_ns_fast_peek_raw(ns, idx, &raw);                          \
    rhs = (RHS);                                                        \
    if (unlikely(! rhs))                                                \
      RESULT_RAISE();                                                   \
    if (unlikely(! acc || ! KOS_##KIND##_CHECK(acc)                     \
                 || (RHS_GUARDED && ! KOS_##KIND##_CHECK(rhs))))        \
      TAILCALL(el_miss, es, insn, value);                               \
    KOS_##KIND##_EXTRACT(acc, rhs, kos_elector_generic_of(insn));       \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b,                   \
                       raw ? acc : NULL, out);                          \
    DROP;                                                               \
    if (out != acc && unlikely(kos_ns_fast_store_steal(ns, idx, out) < 0)) \
      RESULT_RAISE();                                                   \
    RESULT_NEXT(es);                                                    \
  }


/* The operators with a fast path of each kind, applied to a body
   macro that takes (family, KIND, kind, name, KOS_OPERAND_ suffix):
   twelve for ints, six for floats. The body names the operator's
   arithmetic through the kind, KOS_INT_FAST(add) and so on. */
#define KOS_INT_ELECTORS(BODY, FAMILY)                                  \
  BODY(FAMILY, INT, int, add, ADD)                                      \
  BODY(FAMILY, INT, int, subtract, SUBTRACT)                            \
  BODY(FAMILY, INT, int, multiply, MULTIPLY)                            \
  BODY(FAMILY, INT, int, modulo, MODULO)                                \
  BODY(FAMILY, INT, int, bit_and, BIT_AND)                              \
  BODY(FAMILY, INT, int, bit_or, BIT_OR)                                \
  BODY(FAMILY, INT, int, bit_xor, BIT_XOR)                              \
  BODY(FAMILY, INT, int, bit_left, BIT_LEFT)                            \
  BODY(FAMILY, INT, int, bit_right, BIT_RIGHT)                          \
  BODY(FAMILY, INT, int, floor_divide, FLOOR_DIVIDE)                    \
  BODY(FAMILY, INT, int, true_divide, TRUE_DIVIDE)                      \
  BODY(FAMILY, INT, int, power, POWER)

#define KOS_FLOAT_ELECTORS(BODY, FAMILY)                                \
  BODY(FAMILY, FLOAT, float, add, ADD)                                  \
  BODY(FAMILY, FLOAT, float, subtract, SUBTRACT)                        \
  BODY(FAMILY, FLOAT, float, multiply, MULTIPLY)                        \
  BODY(FAMILY, FLOAT, float, modulo, MODULO)                            \
  BODY(FAMILY, FLOAT, float, true_divide, TRUE_DIVIDE)                  \
  BODY(FAMILY, FLOAT, float, floor_divide, FLOOR_DIVIDE)

/* The operators the compiler emits a run of more than two operands
   for, whose steps over C values a fold body names through the kind,
   KOS_INT_STEP(add) and so on. Remainder and the shifts
   are binary forms; a third operand to power is a modulus, so a
   power site of three is never a fold; int true division has no int
   step, its first step being a float already. */
#define KOS_INT_FOLD_ELECTORS(BODY, FAMILY)                             \
  BODY(FAMILY, INT, int, add, ADD)                                      \
  BODY(FAMILY, INT, int, subtract, SUBTRACT)                            \
  BODY(FAMILY, INT, int, multiply, MULTIPLY)                            \
  BODY(FAMILY, INT, int, bit_and, BIT_AND)                              \
  BODY(FAMILY, INT, int, bit_or, BIT_OR)                                \
  BODY(FAMILY, INT, int, bit_xor, BIT_XOR)                              \
  BODY(FAMILY, INT, int, floor_divide, FLOOR_DIVIDE)

#define KOS_FLOAT_FOLD_ELECTORS(BODY, FAMILY)                           \
  BODY(FAMILY, FLOAT, float, add, ADD)                                  \
  BODY(FAMILY, FLOAT, float, subtract, SUBTRACT)                        \
  BODY(FAMILY, FLOAT, float, multiply, MULTIPLY)                        \
  BODY(FAMILY, FLOAT, float, true_divide, TRUE_DIVIDE)                  \
  BODY(FAMILY, FLOAT, float, floor_divide, FLOOR_DIVIDE)

/* A family's table of bodies of one kind by operator, from the list
   that made them; NULL where the operator has no fast path of that
   kind. */
#define KOS_ELECTOR_TABLE_ENTRY(FAMILY, KIND, kind, NAME, OPERAND)      \
  [KOS_OPERAND_##OPERAND] = el_##FAMILY##_##kind##_##NAME,

#define KOS_ELECTOR_TABLE(FAMILY, LIST, kind)                           \
  static const KosThreadFn                                              \
  kos_##FAMILY##_##kind##_bodies[KOS_OPERAND_COUNT] = {                 \
    LIST(KOS_ELECTOR_TABLE_ENTRY, FAMILY)                               \
  };

/* Both kinds' bodies and tables for a family, through its body macro. */
#define KOS_NUMERIC_ELECTORS(BODY, FAMILY)                              \
  KOS_INT_ELECTORS(BODY, FAMILY)                                        \
  KOS_ELECTOR_TABLE(FAMILY, KOS_INT_ELECTORS, int)                      \
  KOS_FLOAT_ELECTORS(BODY, FAMILY)                                      \
  KOS_ELECTOR_TABLE(FAMILY, KOS_FLOAT_ELECTORS, float)

#define KOS_NUMERIC_FOLD_ELECTORS(BODY, FAMILY)                         \
  KOS_INT_FOLD_ELECTORS(BODY, FAMILY)                                   \
  KOS_ELECTOR_TABLE(FAMILY, KOS_INT_FOLD_ELECTORS, int)                 \
  KOS_FLOAT_FOLD_ELECTORS(BODY, FAMILY)                                 \
  KOS_ELECTOR_TABLE(FAMILY, KOS_FLOAT_FOLD_ELECTORS, float)


/* The pair bodies are shared by OBJECT_OPERAND and OBJECT_INPLACE
   under the family name pair. */
KOS_NUMERIC_ELECTORS(KOS_PAIR_ELECTOR, pair)


/* One fold body per operator and kind for the same two families at a
   count above two: every operand of the run of the kind, or a miss;
   the run folded left to right in C, a decline at any step (overflow,
   a zero divisor, an int too wide to read) taking the generic body
   before anything is materialised; then one materialisation, into
   the first operand when the stack is its only owner, in which case
   the rest of the run alone is dropped. */
#define KOS_PAIR_FOLD_ELECTOR(FAMILY, KIND, kind, NAME, OPERAND)        \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    KosSegment **bl = KosEvalState_BLOCK(es);                           \
    size_t count = KosOpCount_COUNT(KosOp_ARG(insn));                   \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, count);        \
    KOS_##KIND##_VARS;                                                  \
    KOS_##KIND##_TYPE res;                                              \
    PyObject *out;                                                      \
    int rc;                                                             \
                                                                        \
    if (unlikely(! args))                                               \
      RESULT_RAISE();                                                   \
    for (size_t i = 0; i < count; i++) {                                \
      if (unlikely(! KOS_##KIND##_CHECK(args[i])))                      \
        TAILCALL(el_miss, es, insn, value);                             \
    }                                                                   \
    KOS_##KIND##_EXTRACT(args[0], args[1], kos_elector_generic_of(insn)); \
    if (unlikely(! KOS_##KIND##_STEP(NAME)(a, b, &res)))                \
      TAILCALL(kos_elector_generic_of(insn), es, insn, value);          \
    for (size_t i = 2; i < count; i++) {                                \
      a = res;                                                          \
      KOS_##KIND##_EXTRACT_ONE(args[i], b, kos_elector_generic_of(insn)); \
      if (unlikely(! KOS_##KIND##_STEP(NAME)(a, b, &res)))              \
        TAILCALL(kos_elector_generic_of(insn), es, insn, value);        \
    }                                                                   \
                                                                        \
    rc = KOS_##KIND##_MATERIALISE(res, args[0], &out);                  \
    if (unlikely(rc < 0))                                               \
      RESULT_RAISE();                                                   \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    if (out == args[0]) {                                               \
      kos_stack_drop_noerr_fast(bl, count - 1);                         \
    } else {                                                            \
      kos_stack_drop_noerr_fast(bl, count);                             \
      if (unlikely(kos_stack_push_steal_fast(bl, out) < 0))             \
        RESULT_RAISE();                                                 \
    }                                                                   \
    RESULT_NEXT(es);                                                    \
  }

KOS_NUMERIC_FOLD_ELECTORS(KOS_PAIR_FOLD_ELECTOR, pair_fold)


/* The top of the stack, borrowed, or NULL with an error set. */
KOS_OP_INLINE PyObject *kos_stack_peek_top(KosEvalState *es) {
  PyObject *const *top = kos_stack_tail_ptr_fast(*KosEvalState_BLOCK(es), 1);
  return top ? top[0] : NULL;
}

/* FAST_INPLACE: the operand is the top of the stack, borrowed for the
   arithmetic and dropped before the store. */
#define KOS_FAST_INPLACE_BODY(FAMILY, KIND, kind, NAME, OPERAND)        \
  KOS_SLOT_ELECTOR(                                                     \
      FAMILY, KIND, kind, NAME,                                         \
      kos_stack_peek_top(es), 1,                                        \
      kos_stack_drop1_fast(KosEvalState_BLOCK(es)))

KOS_NUMERIC_ELECTORS(KOS_FAST_INPLACE_BODY, fast_inplace)


/* FAST_INPLACE_CONST: the operand is the pool constant on the row,
   borrowed; nothing to drop. Its type was checked at the election and
   cannot change, so the body does not guard it. */
#define KOS_FAST_INPLACE_CONST_BODY(FAMILY, KIND, kind, NAME, OPERAND)  \
  KOS_SLOT_ELECTOR(                                                     \
      FAMILY, KIND, kind, NAME,                                         \
      KosExtra_OBJ(row, 2), 0,                                          \
      ((void) 0))

KOS_NUMERIC_ELECTORS(KOS_FAST_INPLACE_CONST_BODY, fast_inplace_const)


/* OBJECT_OPERAND_CONST: the left operand is the top of the stack, the
   right the pool constant on the row, borrowed. The constant's kind was
   checked at the election and cannot change, so only the top is
   guarded. The top is offered as the result's home, as the pair body
   offers its left operand; else the result replaces it. */
#define KOS_OPERAND_CONST_BODY(FAMILY, KIND, kind, NAME, OPERAND)       \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    KosSegment **bl = KosEvalState_BLOCK(es);                           \
    PyObject *const *top = kos_stack_tail_ptr_fast(*bl, 1);             \
    PyObject *rhs = KosExtra_OBJ(row, 1);                               \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    if (unlikely(! top))                                                \
      RESULT_RAISE();                                                   \
    if (unlikely(! KOS_##KIND##_CHECK(top[0])))                         \
      TAILCALL(el_miss, es, insn, value);                               \
    KOS_##KIND##_EXTRACT(top[0], rhs, kos_elector_generic_of(insn));    \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, top[0], out);     \
    if (out != top[0]) {                                                \
      kos_stack_drop1_fast(bl);                                         \
      if (unlikely(kos_stack_push_steal_fast(bl, out) < 0))             \
        RESULT_RAISE();                                                 \
    }                                                                   \
    RESULT_NEXT(es);                                                    \
  }

KOS_NUMERIC_ELECTORS(KOS_OPERAND_CONST_BODY, operand_const)


/* FAST_OPERAND_CONST: the left operand is the fast slot's value,
   peeked borrowed (a slot the peek cannot read misses; the generic
   body derefs and raises), the right the pool constant, borrowed and
   unguarded as above. The slot keeps its value, so no home is offered
   and the result is a fresh push. */
#define KOS_FAST_OPERAND_CONST_BODY(FAMILY, KIND, kind, NAME, OPERAND)  \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *ns = KosEvalState_NAMESPACE(es);                          \
    PyObject *lhs;                                                      \
    PyObject *rhs = KosExtra_OBJ(row, 2);                               \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    lhs = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 1));          \
    if (unlikely(! lhs || ! KOS_##KIND##_CHECK(lhs)))                   \
      TAILCALL(el_miss, es, insn, value);                               \
    KOS_##KIND##_EXTRACT(lhs, rhs, kos_elector_generic_of(insn));       \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, NULL, out);       \
    if (unlikely(kos_stack_push_steal_fast(KosEvalState_BLOCK(es), out) < 0)) \
      RESULT_RAISE();                                                   \
    RESULT_NEXT(es);                                                    \
  }

KOS_NUMERIC_ELECTORS(KOS_FAST_OPERAND_CONST_BODY, fast_operand_const)


/* === Str bodies === */


/* One str body per family for + on an exact-str pair, and for the two
   stack families a join body for a site of more than two operands.
   The guard is the exact-str check, so a str subclass misses and
   keeps its __add__. The left operand or the accumulator is taken
   from its home and handed to str_fast_append, which grows it in
   place when the home held the only reference and concatenates
   otherwise; the result goes back to the same home. A failed append
   has released the left and leaves the home vacant: the stack pair
   shrinks to the right operand, a fast slot or a __slots__ member
   reads as unbound, as a local does in CPython. */


KOS_ELECTOR(pair_str_add) {
  KosSegment **bl = KosEvalState_BLOCK(es);
  PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);
  PyObject *out;

  KOS_ELECTOR_PAIR_SAFETYNET(insn);
  if (unlikely(! args))
    RESULT_RAISE();
  if (unlikely(! (PyUnicode_CheckExact(args[0])
                  && PyUnicode_CheckExact(args[1]))))
    TAILCALL(el_miss, es, insn, value);

  if (unlikely(str_fast_append(args[0], args[1], &out) < 0)) {
    kos_stack_settle2_fast(bl, NULL);
    RESULT_RAISE();
  }
  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
  kos_stack_settle2_fast(bl, out);
  RESULT_NEXT(es);
}


/* The join body: every operand of the run an exact str, or a miss;
   one allocation replaces the run. */
KOS_ELECTOR(pair_str_join) {
  KosSegment **bl = KosEvalState_BLOCK(es);
  size_t count = KosOpCount_COUNT(KosOp_ARG(insn));
  PyObject *const *args = kos_stack_tail_ptr_fast(*bl, count);
  PyObject *out;
  int rc;

  if (unlikely(! args))
    RESULT_RAISE();
  rc = str_fast_join(args, count, &out);
  if (unlikely(rc <= 0)) {
    if (rc < 0)
      RESULT_RAISE();
    TAILCALL(el_miss, es, insn, value);
  }
  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
  kos_stack_drop_noerr_fast(bl, count);
  if (unlikely(kos_stack_push_steal_fast(bl, out) < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/* The str body of a family whose accumulator is a fast slot: a raw
   slot's value is taken, the slot left empty until the store, so the
   append can grow it in place; a ref slot keeps its reference and the
   append concatenates. */
#define KOS_SLOT_STR_ELECTOR(FAMILY, RHS, RHS_GUARDED, DROP)            \
  KOS_ELECTOR(FAMILY##_str_add) {                                       \
    const KosExtra *row = KosOp_ROW(insn);                              \
    size_t idx = (size_t) KosExtra_IMM(row, 1);                         \
    PyObject *ns = KosEvalState_NAMESPACE(es);                          \
    PyObject *acc;                                                      \
    PyObject *rhs;                                                      \
    PyObject *left;                                                     \
    PyObject *out;                                                      \
    int raw;                                                            \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    acc = kos_ns_fast_peek_raw(ns, idx, &raw);                          \
    rhs = (RHS);                                                        \
    if (unlikely(! rhs))                                                \
      RESULT_RAISE();                                                   \
    if (unlikely(! acc || ! PyUnicode_CheckExact(acc)                   \
                 || (RHS_GUARDED && ! PyUnicode_CheckExact(rhs))))      \
      TAILCALL(el_miss, es, insn, value);                               \
                                                                        \
    left = acc;                                                         \
    if (raw)                                                            \
      KosNamespace_GET_FAST_VAL(ns, idx) = NULL;                        \
    else                                                                \
      Py_INCREF(left);                                                  \
    if (unlikely(str_fast_append(left, rhs, &out) < 0))                 \
      RESULT_RAISE();                                                   \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    DROP;                                                               \
    if (unlikely(kos_ns_fast_store_steal(ns, idx, out) < 0))            \
      RESULT_RAISE();                                                   \
    RESULT_NEXT(es);                                                    \
  }

KOS_SLOT_STR_ELECTOR(
    fast_inplace,
    kos_stack_peek_top(es), 1,
    kos_stack_drop1_fast(KosEvalState_BLOCK(es)))
KOS_SLOT_STR_ELECTOR(
    fast_inplace_const,
    KosExtra_OBJ(row, 2), 0,
    ((void) 0))


/* === Compare families === */


/**
   CHECK_COMPARE without an election: chain-compare the count operands
   under the selector, drop them, jump on failure.
*/
KOS_ELECTOR(check_compare_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  size_t count = (size_t) KosExtra_IMM(row, 1);
  PyObject *const *args;
  int res;

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(count < 2)) {
    PyErr_SetString(PyExc_TypeError,
                    "CHECK_COMPARE requires at least 2 elements");
    RESULT_RAISE();
  }

  args = KosEvalState_StackTailPtr(es, count);
  if (unlikely(! args))
    RESULT_RAISE();

  res = kos_compare_chain((KosCompare) KosExtra_IMM(row, 0), args, count);
  KosEvalState_StackDropNoErr(es, count);
  if (unlikely(res < 0))
    RESULT_RAISE();
  if (res == 0)
    RESULT_JUMP_AT(es, KosExtra_TARGET(row, 2));
  RESULT_NEXT(es);
}


/**
   OBJECT_COMPARE without an election: chain-compare the count operands
   under the selector and push the result.
*/
KOS_ELECTOR(object_compare_generic) {
  uint16_t arg = KosOp_ARG(insn);
  size_t count = (size_t) KosCmpCount_COUNT(arg);
  PyObject *const *args;
  PyObject *result;

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(count < 2)) {
    PyErr_SetString(PyExc_TypeError,
                    "OBJECT_COMPARE requires at least 2 elements");
    RESULT_RAISE();
  }

  args = KosEvalState_StackTailPtr(es, count);
  if (unlikely(! args))
    RESULT_RAISE();

  result = kos_compare_reduce((KosCompare) KosCmpCount_SEL(arg), args, count);
  KosEvalState_StackDropNoErr(es, count);
  if (unlikely(! result))
    RESULT_RAISE();
  RESULT_STEAL(es, result);
}


/* One body per kind for each compare opcode: the stack pair under a
   richcompare selector, settled on the kind's compare over the
   extracted values. CHECK_COMPARE takes its selector from the row,
   drops the pair, and jumps when the compare fails; OBJECT_COMPARE
   takes it from the packed argument and replaces the pair with the
   bool. SEL reads the selector after the family's prologue (ROW)
   has run; SETTLE takes the compare's result. */
#define KOS_COMPARE_ELECTOR(FAMILY, KIND, kind, ROW, SEL, SETTLE)       \
  KOS_ELECTOR(FAMILY##_##kind) {                                        \
    ROW;                                                                \
    KosSegment **bl = KosEvalState_BLOCK(es);                           \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);            \
    KOS_##KIND##_VARS;                                                  \
    int held;                                                           \
                                                                        \
    if (unlikely(! args))                                               \
      RESULT_RAISE();                                                   \
    if (unlikely(! KOS_##KIND##_GUARD(args[0], args[1])))               \
      TAILCALL(el_miss, es, insn, value);                               \
    KOS_##KIND##_EXTRACT(args[0], args[1], el_##FAMILY##_generic);      \
                                                                        \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    held = KOS_##KIND##_COMPARE((KosCompare) (SEL), a, b);              \
    SETTLE(held);                                                       \
  }

#define KOS_CHECK_COMPARE_SETTLE(held)                                  \
  do {                                                                  \
    kos_stack_drop_noerr_fast(bl, 2);                                   \
    if (held)                                                           \
      RESULT_NEXT(es);                                                  \
    RESULT_JUMP_AT(es, KosExtra_TARGET(row, 2));                        \
  } while (0)

#define KOS_OBJECT_COMPARE_SETTLE(held)                                 \
  do {                                                                  \
    kos_stack_replace2_fast(bl, PyBool_FromLong(held));                 \
    RESULT_NEXT(es);                                                    \
  } while (0)

#define KOS_CHECK_COMPARE_ELECTOR(KIND, kind)                           \
  KOS_COMPARE_ELECTOR(check_compare, KIND, kind,                        \
                      const KosExtra *row = KosOp_ROW(insn),            \
                      KosExtra_IMM(row, 0), KOS_CHECK_COMPARE_SETTLE)

#define KOS_OBJECT_COMPARE_ELECTOR(KIND, kind)                          \
  KOS_COMPARE_ELECTOR(object_compare, KIND, kind,                       \
                      uint16_t arg = KosOp_ARG(insn),                   \
                      KosCmpCount_SEL(arg), KOS_OBJECT_COMPARE_SETTLE)

KOS_CHECK_COMPARE_ELECTOR(INT, int)
KOS_CHECK_COMPARE_ELECTOR(FLOAT, float)
KOS_OBJECT_COMPARE_ELECTOR(INT, int)
KOS_OBJECT_COMPARE_ELECTOR(FLOAT, float)


/* The classification of a compare site: two operands on the stack
   under a richcompare selector, the int body for two exact ints, the
   float body for two exact floats, else NULL. */
static inline KosThreadFn kos_elector_compare_classify(
    KosEvalState *es,
    KosCompare sel,
    size_t count,
    KosThreadFn int_body,
    KosThreadFn float_body) {

  KosSegment *s = *KosEvalState_BLOCK(es);
  PyObject *const *args;

  if (sel > KOS_COMPARE_GE || count != 2 || ! s || s->len < 2)
    return NULL;
  args = kos_stack_tail_ptr_fast(s, 2);
  if (PyLong_CheckExact(args[0]) && PyLong_CheckExact(args[1]))
    return int_body;
  if (PyFloat_CheckExact(args[0]) && PyFloat_CheckExact(args[1]))
    return float_body;
  return NULL;
}


static inline KosThreadFn kos_elector_check_compare_classify(
    KosEvalState *es,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  return kos_elector_compare_classify(
      es, (KosCompare) KosExtra_IMM(row, 0), (size_t) KosExtra_IMM(row, 1),
      el_check_compare_int, el_check_compare_float);
}


static inline KosThreadFn kos_elector_object_compare_classify(
    KosEvalState *es,
    void *insn) {

  uint16_t arg = KosOp_ARG(insn);
  return kos_elector_compare_classify(
      es, (KosCompare) KosCmpCount_SEL(arg), (size_t) KosCmpCount_COUNT(arg),
      el_object_compare_int, el_object_compare_float);
}


static inline KosThreadFn kos_elector_num_body(
    KosOperand op,
    PyObject *l,
    PyObject *r,
    const KosThreadFn *int_table,
    const KosThreadFn *float_table,
    KosThreadFn str_add);


/* === FAST_ATTR_INPLACE family === */


/* The address of the direct location the field slot resolved for an
   attribute site: a __slots__ member or an inline instance value. NULL
   for any other mode, for an inline value the instance no longer
   holds (a materialised dict, an index past its capacity) and for an
   unset location, so a non-NULL address holds a value. The type
   guard the head re-checks is what makes the address valid: a type
   with generic getattr and setattr and no descriptor over the name,
   so a store through the address is what setattr would do. */
KOS_OP_INLINE PyObject **kos_field_direct_addr(
    PyObject *obj, KosFieldMode mode, uint32_t index) {

  PyObject **addr = NULL;

  if (mode == KOS_FIELD_SLOT) {
    addr = (PyObject **) ((char *) obj + index);
  }
#if PY_MINOR_VERSION >= 11
  else if (mode == KOS_FIELD_INSTANCE_VALUE) {
    int active;
    uint32_t cap;
    PyObject **slots = kos_inline_value_slots(obj, &active, &cap);
    if (! active || index >= cap)
      return NULL;
    addr = &slots[index];
  }
#endif
  return (addr && *addr) ? addr : NULL;
}


/**
   FAST_ATTR_INPLACE without an election: the handler as it stood,
   reducing obj.attr in place through the field cache.
*/
KOS_ELECTOR(fast_attr_inplace_generic) {
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  int count = (int) KosOpCount_COUNT(KosExtra_IMM(row, 0));
  size_t idx = (size_t) KosExtra_IMM(row, 1);
  PyObject *attr = KosExtra_OBJ(row, 2);
  PyObject *ns = KosEvalState_NAMESPACE(es);
  KosCacheSlot *slot = KosOp_SLOT(insn);
  PyObject *obj;
  PyObject *const *args;
  int res;

  KOS_OP_REQUIRE_NS(ns);
  kos_elector_generic_tick(slot ? slot + 1 : NULL);

  obj = kos_ns_fast_deref(ns, idx);
  if (! obj)
    RESULT_RAISE();

  args = count ? KosEvalState_StackTailPtr(es, (size_t) count) : NULL;
  if (count && ! args) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  res = kos_attr_inplace_cached(slot, obj, attr, op, args, count);
  KosEvalState_StackDropNoErr(es, (size_t) count);
  Py_DECREF(obj);
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(es);
}


/* The tail of an attribute body once the result is in hand: the one
   operand dropped, then the result, owned, swapped into the location
   unless it was written in place. Nothing runs between the head's
   load and this store, so the location is the one the head read. */
#define KOS_ATTR_ELECTOR_STORE(addr, acc, out)                          \
  do {                                                                  \
    kos_stack_drop1_fast(KosEvalState_BLOCK(es));                       \
    if ((out) != (acc)) {                                               \
      *(addr) = (out);                                                  \
      Py_DECREF(acc);                                                   \
    }                                                                   \
  } while (0)


/* The head of an attribute body: the receiver peeked from its fast
   slot, the operand peeked from the stack, the field slot's guard
   re-checked against the receiver's type, and the direct location's
   address taken with the accumulator borrowed from it. Any of those
   failing is a miss. */
#define KOS_ATTR_ELECTOR_HEAD(rhs, addr, acc)                           \
  do {                                                                  \
    KosCacheSlot *kos_ah_field_ = KosOp_SLOT(insn);                     \
    PyObject *kos_ah_ns_ = KosEvalState_NAMESPACE(es);                  \
    PyObject *kos_ah_obj_;                                              \
    KOS_OP_REQUIRE_NS(kos_ah_ns_);                                      \
    kos_ah_obj_ = kos_ns_fast_peek(kos_ah_ns_,                          \
                                   (size_t) KosExtra_IMM(row, 1));      \
    (rhs) = kos_stack_peek_top(es);                                     \
    if (unlikely(! (rhs)))                                              \
      RESULT_RAISE();                                                   \
    if (unlikely(! kos_ah_obj_ || kos_ah_field_->kind != KOS_CACHE_FIELD \
                 || ! field_guard_ok(Py_TYPE(kos_ah_obj_), kos_ah_field_))) \
      TAILCALL(el_miss, es, insn, value);                               \
    (addr) = kos_field_direct_addr(kos_ah_obj_, kos_ah_field_->u.field.mode, \
                                   kos_ah_field_->u.field.index);       \
    if (unlikely(! (addr)))                                             \
      TAILCALL(el_miss, es, insn, value);                               \
    (acc) = *(addr);                                                    \
  } while (0)


/* One body per operator and kind for an attribute site: the head,
   the kind's guard on the accumulator and the operand, the extraction
   declining to the family's generic body, the accumulator offered as
   the result's home, the store. */
#define KOS_ATTR_ELECTOR(FAMILY, KIND, kind, NAME, OPERAND)             \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *rhs, *acc, *out;                                          \
    PyObject **addr;                                                    \
    KOS_##KIND##_VARS;                                                  \
                                                                        \
    KOS_ATTR_ELECTOR_HEAD(rhs, addr, acc);                              \
    if (unlikely(! KOS_##KIND##_GUARD(acc, rhs)))                       \
      TAILCALL(el_miss, es, insn, value);                               \
    KOS_##KIND##_EXTRACT(acc, rhs, el_##FAMILY##_generic);              \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, acc, out);        \
    KOS_ATTR_ELECTOR_STORE(addr, acc, out);                             \
    RESULT_NEXT(es);                                                    \
  }

KOS_NUMERIC_ELECTORS(KOS_ATTR_ELECTOR, fast_attr_inplace)


/**
   The str body of an attribute site. A __slots__ member is taken and
   the append grows it in place when the instance held the only
   reference; a failed append leaves the member vacant, so the
   attribute reads as unset. An inline instance value is concatenated
   and swapped: a vacated inline value would leave the instance's
   insertion order naming an empty cell, bookkeeping the direct store
   does not keep.
*/
KOS_ELECTOR(fast_attr_inplace_str_add) {
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *rhs, *acc, *out;
  PyObject **addr;

  KOS_ATTR_ELECTOR_HEAD(rhs, addr, acc);
  if (unlikely(! PyUnicode_CheckExact(acc) || ! PyUnicode_CheckExact(rhs)))
    TAILCALL(el_miss, es, insn, value);

  if (KosOp_SLOT(insn)->u.field.mode == KOS_FIELD_SLOT) {
    *addr = NULL;
    if (unlikely(str_fast_append(acc, rhs, &out) < 0))
      RESULT_RAISE();
    *addr = out;
  } else {
    out = PyUnicode_Concat(acc, rhs);
    if (unlikely(! out))
      RESULT_RAISE();
    *addr = out;
    Py_DECREF(acc);
  }
  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
  kos_stack_drop1_fast(KosEvalState_BLOCK(es));
  RESULT_NEXT(es);
}


/* The classification of a FAST_ATTR_INPLACE site: one operand, the
   field slot resolved to a direct location for the receiver's type,
   and the accumulator and operand an exact int, float, or str pair. */
static inline KosThreadFn kos_elector_fast_attr_inplace_classify(
    KosEvalState *es,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  KosCacheSlot *field = KosOp_SLOT(insn);
  KosSegment *s = *KosEvalState_BLOCK(es);
  PyObject *obj = kos_ns_fast_peek(
      KosEvalState_NAMESPACE(es), (size_t) KosExtra_IMM(row, 1));
  PyObject *rhs = (s && s->len >= 1) ? kos_stack_tail_ptr_fast(s, 1)[0] : NULL;
  PyObject **addr;

  if (KosOpCount_COUNT(KosExtra_IMM(row, 0)) != 1 || ! obj || ! rhs
      || ! field || field->kind != KOS_CACHE_FIELD
      || ! field_guard_ok(Py_TYPE(obj), field))
    return NULL;
  addr = kos_field_direct_addr(obj, field->u.field.mode, field->u.field.index);
  return kos_elector_num_body(
      (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0)),
      addr ? *addr : NULL, rhs,
      kos_fast_attr_inplace_int_bodies, kos_fast_attr_inplace_float_bodies,
      el_fast_attr_inplace_str_add);
}


/**
   The electing function: the field slot must already hold a direct
   location, which the generic body's first execution resolves, so a
   fresh site runs generic once and elects on its second execution
   through the tick; a site the classification declines elects the
   generic body and tries again every 256 executions like the rest.
*/
KOS_ELECTOR(fast_attr_inplace_elect) {
  KosCacheSlot *slot = KosOp_SLOT(insn);
  KosThreadFn fn = kos_elector_fast_attr_inplace_classify(es, insn);

  if (! fn)
    fn = el_fast_attr_inplace_generic;
  kos_elector_elect(slot ? slot + 1 : NULL, fn);
  TAILCALL(fn, es, insn, value);
}


/* === Election === */


/* The body a site elects for two borrowed operands: the operator's
   entry in the family's int table when both are exact ints, in its
   float table when both are exact floats, the family's str body for
   + on two exact strs, else NULL. */
static inline KosThreadFn kos_elector_num_body(
    KosOperand op,
    PyObject *l,
    PyObject *r,
    const KosThreadFn *int_table,
    const KosThreadFn *float_table,
    KosThreadFn str_add) {

  if (op >= KOS_OPERAND_COUNT || ! l || ! r)
    return NULL;
  if (PyLong_CheckExact(l) && PyLong_CheckExact(r))
    return int_table[op];
  if (PyFloat_CheckExact(l) && PyFloat_CheckExact(r))
    return float_table[op];
  if (op == KOS_OPERAND_ADD
      && PyUnicode_CheckExact(l) && PyUnicode_CheckExact(r))
    return str_add;
  return NULL;
}


/* The body a stack site of more than two operands elects: the
   operator's fold body of the kind every operand is, the str join
   for + over exact strs, else NULL. */
static inline KosThreadFn kos_elector_fold_body(
    KosOperand op,
    PyObject *const *args,
    size_t count) {

  const KosThreadFn *table = NULL;
  PyTypeObject *tp = Py_TYPE(args[0]);

  if (op >= KOS_OPERAND_COUNT)
    return NULL;
  if (tp == &PyLong_Type)
    table = kos_pair_fold_int_bodies;
  else if (tp == &PyFloat_Type)
    table = kos_pair_fold_float_bodies;
  else if (! (op == KOS_OPERAND_ADD && tp == &PyUnicode_Type))
    return NULL;
  for (size_t i = 1; i < count; i++) {
    if (Py_TYPE(args[i]) != tp)
      return NULL;
  }
  return table ? table[op] : el_pair_str_join;
}


/* The classification of a stack-pair site: binary with the pair on
   the stack, or a run of more than two of one kind. */
static inline KosThreadFn kos_elector_pair_classify(
    KosEvalState *es,
    void *insn) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  size_t count = KosOpCount_COUNT(arg);
  KosSegment *s = *KosEvalState_BLOCK(es);
  PyObject *const *args;

  if (count < 2 || ! s || s->len < count)
    return NULL;
  args = kos_stack_tail_ptr_fast(s, count);
  if (count > 2)
    return kos_elector_fold_body(op, args, count);
  return kos_elector_num_body(
      op, args[0], args[1],
      kos_pair_int_bodies, kos_pair_float_bodies, el_pair_str_add);
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

  return kos_elector_num_body(
      (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0)), acc, rhs,
      kos_fast_inplace_int_bodies, kos_fast_inplace_float_bodies,
      el_fast_inplace_str_add);
}


/* The classification of a FAST_INPLACE_CONST site: the peeked slot
   value and the pool constant. */
static inline KosThreadFn kos_elector_fast_inplace_const_classify(
    KosEvalState *es,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *acc = kos_ns_fast_peek(
      KosEvalState_NAMESPACE(es), (size_t) KosExtra_IMM(row, 1));
  PyObject *rhs = KosExtra_OBJ(row, 2);
  long long b;

  // a constant the int body could never read would decline on every
  // execution; the site stays generic
  if (PyLong_CheckExact(rhs) && ! kos_long_as_ll(rhs, &b))
    return NULL;
  return kos_elector_num_body(
      (KosOperand) KosExtra_IMM(row, 0), acc, rhs,
      kos_fast_inplace_const_int_bodies,
      kos_fast_inplace_const_float_bodies,
      el_fast_inplace_const_str_add);
}



/* A family's electing function, reached from an empty slot or an
   ungated site: classify the operands once, settle the slot with the
   body classified or the generic body, and dispatch. */
#define KOS_ELECTOR_ELECT(FAMILY, CLASSIFY)                             \
  KOS_ELECTOR(FAMILY##_elect) {                                         \
    KosThreadFn fn = CLASSIFY(es, insn);                                \
                                                                        \
    if (! fn)                                                           \
      fn = el_##FAMILY##_generic;                                       \
    kos_elector_elect(KosOp_SLOT(insn), fn);                            \
    TAILCALL(fn, es, insn, value);                                      \
  }

/* The classification of an OBJECT_OPERAND_CONST site: the top of the
   stack and the pool constant. A constant the int body could never
   read would decline on every execution; the site stays generic. */
static inline KosThreadFn kos_elector_operand_const_classify(
    KosEvalState *es,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *lhs = kos_stack_peek_top(es);
  PyObject *rhs = KosExtra_OBJ(row, 1);
  long long b;

  if (! lhs) {
    PyErr_Clear();
    return NULL;
  }
  if (PyLong_CheckExact(rhs) && ! kos_long_as_ll(rhs, &b))
    return NULL;
  return kos_elector_num_body(
      (KosOperand) KosExtra_IMM(row, 0), lhs, rhs,
      kos_operand_const_int_bodies, kos_operand_const_float_bodies, NULL);
}


/* The classification of a FAST_OPERAND_CONST site: the peeked slot
   value and the pool constant, with the same rule on the constant. */
static inline KosThreadFn kos_elector_fast_operand_const_classify(
    KosEvalState *es,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *lhs = kos_ns_fast_peek(
      KosEvalState_NAMESPACE(es), (size_t) KosExtra_IMM(row, 1));
  PyObject *rhs = KosExtra_OBJ(row, 2);
  long long b;

  if (PyLong_CheckExact(rhs) && ! kos_long_as_ll(rhs, &b))
    return NULL;
  return kos_elector_num_body(
      (KosOperand) KosExtra_IMM(row, 0), lhs, rhs,
      kos_fast_operand_const_int_bodies, kos_fast_operand_const_float_bodies,
      NULL);
}


KOS_ELECTOR_ELECT(operand, kos_elector_pair_classify)
KOS_ELECTOR_ELECT(inplace, kos_elector_pair_classify)
KOS_ELECTOR_ELECT(fast_inplace, kos_elector_fast_inplace_classify)
KOS_ELECTOR_ELECT(fast_inplace_const, kos_elector_fast_inplace_const_classify)
KOS_ELECTOR_ELECT(operand_const, kos_elector_operand_const_classify)
KOS_ELECTOR_ELECT(fast_operand_const, kos_elector_fast_operand_const_classify)
KOS_ELECTOR_ELECT(check_compare, kos_elector_check_compare_classify)
KOS_ELECTOR_ELECT(object_compare, kos_elector_object_compare_classify)


#undef KOS_ELECTOR_ELECT
#undef KOS_ATTR_ELECTOR
#undef KOS_ATTR_ELECTOR_HEAD
#undef KOS_ATTR_ELECTOR_STORE
#undef KOS_OBJECT_COMPARE_ELECTOR
#undef KOS_CHECK_COMPARE_ELECTOR
#undef KOS_OBJECT_COMPARE_SETTLE
#undef KOS_CHECK_COMPARE_SETTLE
#undef KOS_COMPARE_ELECTOR
#undef KOS_SLOT_STR_ELECTOR
#undef KOS_FAST_INPLACE_CONST_BODY
#undef KOS_OPERAND_CONST_BODY
#undef KOS_FAST_OPERAND_CONST_BODY
#undef KOS_FAST_INPLACE_BODY
#undef KOS_PAIR_FOLD_ELECTOR
#undef KOS_NUMERIC_FOLD_ELECTORS
#undef KOS_NUMERIC_ELECTORS
#undef KOS_ELECTOR_TABLE
#undef KOS_ELECTOR_TABLE_ENTRY
#undef KOS_FLOAT_FOLD_ELECTORS
#undef KOS_INT_FOLD_ELECTORS
#undef KOS_FLOAT_ELECTORS
#undef KOS_INT_ELECTORS
#undef KOS_SLOT_ELECTOR
#undef KOS_PAIR_ELECTOR
#undef KOS_ELECTOR_RESULT
#undef KOS_FLOAT_STEP
#undef KOS_FLOAT_FAST
#undef KOS_FLOAT_MATERIALISE
#undef KOS_FLOAT_COMPARE
#undef KOS_FLOAT_EXTRACT
#undef KOS_FLOAT_EXTRACT_ONE
#undef KOS_FLOAT_GUARD
#undef KOS_FLOAT_CHECK
#undef KOS_FLOAT_VARS
#undef KOS_FLOAT_TYPE
#undef KOS_INT_STEP
#undef KOS_INT_FAST
#undef KOS_INT_MATERIALISE
#undef KOS_INT_COMPARE
#undef KOS_INT_EXTRACT
#undef KOS_INT_EXTRACT_ONE
#undef KOS_INT_GUARD
#undef KOS_INT_CHECK
#undef KOS_INT_VARS
#undef KOS_INT_TYPE
#undef KOS_ELECTOR_PAIR_SAFETYNET
#undef KOS_ELECTOR_DECL


// The end.
