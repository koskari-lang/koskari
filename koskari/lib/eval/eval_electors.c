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
      KosContext *ctx, void *insn, PyObject *value)

#define KOS_ELECTOR_DECL(name)                                          \
  static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult el_##name(          \
      KosContext *ctx, void *insn, PyObject *value);


/* Guard misses since the election before a site demotes. Shared by the
   elector and item caches (eval/eval_item.c). */
#define KOS_ELECTION_MISS_LIMIT 8


/* Start an election's accounting over in the given mode. */
static inline void kos_election_reset(KosElection *el, KosElectorMode mode) {
  el->misses = 0;
  el->ticks = 0;
  el->mode = (uint8_t) mode;
}


/* Count a guard miss at an elected site. At the limit the accounting
   resets as megamorphic and the answer is 1: the caller demotes the
   site's body.

   :return: 1 when the site demotes, else 0 */
static inline int kos_election_miss(KosElection *el) {
  uint8_t n;

  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_MISS);
  n = (uint8_t) (el->misses + 1);
  if (n >= KOS_ELECTION_MISS_LIMIT) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_BYPASS);
    kos_election_reset(el, KOS_ELECTOR_MEGAMORPHIC);
    return 1;
  }
  el->misses = n;
  return 0;
}


/* Count one slow-path execution at a site.

   :return: 1 when the count wraps and the caller empties the slot for a
     re-election, else 0 */
static inline int kos_election_tick(KosElection *el) {
  return ++el->ticks == 0;
}


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
  kos_election_reset(&slot->u.elector.election, KOS_ELECTOR_ELECTED);
  slot->kind = KOS_CACHE_ELECTOR;
}


/* An int constant's C value, read at the election for the body to
   take off its site; a site with no slot runs the body once from the
   classification and reads the constant itself. */
static inline void kos_elector_keep_const(KosCacheSlot *slot, long long b) {
  if (slot)
    slot->u.elector.index = b;
}


/* Count a guard miss at an elected site. At limit the slot demotes:
   the generic body becomes the elected one, so the site runs its
   family's ladder on every execution until the generic body's own
   count empties the slot for a re-election. */
static inline void kos_elector_miss(KosCacheSlot *slot, KosThreadFn generic) {
  if (! slot || slot->kind != KOS_CACHE_ELECTOR)
    return;

  if (kos_election_miss(&slot->u.elector.election))
    slot->u.elector.fn = generic;
}


/* One execution of a generic body, reached by demotion or by a decline
   from an elected body: the slow path counts in its own byte, and when
   it wraps the slot empties, so the next execution classifies again.
   A decline is not a guard miss and never moves the site toward
   demotion. */
static inline void kos_elector_generic_tick(KosCacheSlot *slot) {
  if (slot && slot->kind == KOS_CACHE_ELECTOR
      && kos_election_tick(&slot->u.elector.election))
    slot->kind = KOS_CACHE_EMPTY;
}


/* === The families' generic bodies === */


KOS_ELECTOR_DECL(operand_generic)
KOS_ELECTOR_DECL(inplace_generic)
KOS_ELECTOR_DECL(fast_inplace_generic)
KOS_ELECTOR_DECL(fast_inplace_const_generic)
KOS_ELECTOR_DECL(operand_const_generic)
KOS_ELECTOR_DECL(fast_operand_const_generic)
KOS_ELECTOR_DECL(operand_fast_generic)
KOS_ELECTOR_DECL(fast_operand_fast_generic)
KOS_ELECTOR_DECL(fast_item_const_generic)
KOS_ELECTOR_DECL(item_const_generic)
KOS_ELECTOR_DECL(item_const_store_generic)
KOS_ELECTOR_DECL(item_const_store_const_generic)
KOS_ELECTOR_DECL(item_const_store_fast_generic)
KOS_ELECTOR_DECL(fast_item_const_store_generic)
KOS_ELECTOR_DECL(fast_item_const_store_const_generic)
KOS_ELECTOR_DECL(fast_item_const_store_fast_generic)
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
    case KOS_OP_OBJECT_OPERAND_FAST:
      return el_operand_fast_generic;
    case KOS_OP_FAST_OPERAND_FAST:
      return el_fast_operand_fast_generic;
    case KOS_OP_FAST_ITEM_CONST:
      return el_fast_item_const_generic;
    case KOS_OP_ITEM_CONST:
      return el_item_const_generic;
    case KOS_OP_ITEM_CONST_STORE:
      return el_item_const_store_generic;
    case KOS_OP_ITEM_CONST_STORE_CONST:
      return el_item_const_store_const_generic;
    case KOS_OP_ITEM_CONST_STORE_FAST:
      return el_item_const_store_fast_generic;
    case KOS_OP_FAST_ITEM_CONST_STORE:
      return el_fast_item_const_store_generic;
    case KOS_OP_FAST_ITEM_CONST_STORE_CONST:
      return el_fast_item_const_store_const_generic;
    case KOS_OP_FAST_ITEM_CONST_STORE_FAST:
      return el_fast_item_const_store_fast_generic;
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
  KOS_SYNC_PC(ctx, insn);
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(kos_object_operand(ctx, op, count) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


/**
   OBJECT_INPLACE without an election: the in-place reduce out of line.
*/
KOS_ELECTOR(inplace_generic) {
  KOS_SYNC_PC(ctx, insn);
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  int count = (int) KosOpCount_COUNT(arg);

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(kos_object_inplace(ctx, op, count) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


/**
   FAST_INPLACE without an election: slot = slot OP top-of-stack through
   the in-place reduce.
*/
KOS_ELECTOR(fast_inplace_generic) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosExtra_IMM(row, 0);
  size_t idx = (size_t) KosExtra_IMM(row, 1);

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(kos_fast_inplace_operand(ctx, op, idx) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


/**
   OBJECT_OPERAND_CONST without an election: the stack top and the
   row's constant through the operator's reduce, the result replacing
   the top.
*/
KOS_ELECTOR(operand_const_generic) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosSegment **bl = KosContext_BLOCK(ctx);
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
  RESULT_STEAL(ctx, result);
}


/**
   FAST_OPERAND_CONST without an election: the slot's value (derefed,
   so an empty slot raises and a ref runs its own deref) and the row's
   constant through the operator's reduce, the result pushed.
*/
KOS_ELECTOR(fast_operand_const_generic) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosContext_NAMESPACE(ctx);
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
  RESULT_STEAL(ctx, result);
}


/**
   OBJECT_OPERAND_FAST without an election: the stack top and the
   slot's value (derefed, so an empty slot raises and a ref runs its
   own deref) through the operator's reduce, the result replacing the
   top.
*/
KOS_ELECTOR(operand_fast_generic) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosSegment **bl = KosContext_BLOCK(ctx);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  PyObject *const *top = kos_stack_tail_ptr_fast(*bl, 1);
  PyObject *pair[2];
  PyObject *result;

  KOS_OP_REQUIRE_NS(ns);
  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(! top))
    RESULT_RAISE();
  pair[1] = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 1));
  if (unlikely(! pair[1]))
    RESULT_RAISE();
  pair[0] = top[0];
  result = kos_operand_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  Py_DECREF(pair[1]);
  if (unlikely(! result))
    RESULT_RAISE();
  kos_stack_drop1_fast(bl);
  RESULT_STEAL(ctx, result);
}


/**
   FAST_OPERAND_FAST without an election: both slots' values (derefed)
   through the operator's reduce, the result pushed.
*/
KOS_ELECTOR(fast_operand_fast_generic) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  PyObject *pair[2];
  PyObject *result;

  KOS_OP_REQUIRE_NS(ns);
  kos_elector_generic_tick(KosOp_SLOT(insn));
  pair[0] = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 1));
  if (unlikely(! pair[0]))
    RESULT_RAISE();
  pair[1] = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 2));
  if (unlikely(! pair[1])) {
    Py_DECREF(pair[0]);
    RESULT_RAISE();
  }
  result = kos_operand_reduce((KosOperand) KosExtra_IMM(row, 0), pair, 2);
  Py_DECREF(pair[0]);
  Py_DECREF(pair[1]);
  if (unlikely(! result))
    RESULT_RAISE();
  RESULT_STEAL(ctx, result);
}


/**
   FAST_INPLACE_CONST without an election: slot = slot OP constant
   through the in-place reduce.
*/
KOS_ELECTOR(fast_inplace_const_generic) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosExtra_IMM(row, 0);
  size_t idx = (size_t) KosExtra_IMM(row, 1);

  KOS_OP_REQUIRE_NS(KosContext_NAMESPACE(ctx));
  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(kos_fast_inplace_apply(ctx, op, idx, KosExtra_OBJ(row, 2)) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


/**
   The shared miss function: a type-guard miss at an elected site
   counts toward the demotion and runs the site's generic body for
   this execution.
*/
KOS_ELECTOR(miss) {
  KOS_SYNC_PC(ctx, insn);
  KosThreadFn generic = kos_elector_generic_of(insn);

  kos_elector_miss(kos_elector_slot_of(insn), generic);
  TAILCALL(generic, ctx, insn, value);
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
      TAILCALL(DECLINE, ctx, insn, value);                              \
  } while (0)
#define KOS_INT_EXTRACT(l, r, DECLINE)                                  \
  do {                                                                  \
    if (unlikely(! kos_long_as_ll((l), &a) || ! kos_long_as_ll((r), &b))) \
      TAILCALL(DECLINE, ctx, insn, value);                              \
  } while (0)
/* the right operand is the site's pool constant, read once at the
   election into the slot, so only the left is extracted here; a site
   with no slot runs the elected body straight from its classification
   and reads the constant itself */
#define KOS_INT_EXTRACT_CONST(l, r, DECLINE)                            \
  do {                                                                  \
    KosCacheSlot *kos_ec_slot_ = KosOp_SLOT(insn);                      \
    if (unlikely(! kos_long_as_ll((l), &a)))                            \
      TAILCALL(DECLINE, ctx, insn, value);                              \
    if (likely(kos_ec_slot_))                                           \
      b = kos_ec_slot_->u.elector.index;                                \
    else if (unlikely(! kos_long_as_ll((r), &b)))                       \
      TAILCALL(DECLINE, ctx, insn, value);                              \
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
/* a float constant's extract is one load; nothing to keep in the slot */
#define KOS_FLOAT_EXTRACT_CONST  KOS_FLOAT_EXTRACT
#define KOS_FLOAT_COMPARE       kos_double_compare
#define KOS_FLOAT_MATERIALISE   kos_float_fast_result
#define KOS_FLOAT_FAST(NAME)    float_fast_##NAME##_d
#define KOS_FLOAT_STEP(NAME)    kos_d_##NAME


/* The tail of every numeric body once both operands are C values: the
   operator's arithmetic, a decline (overflow, a negative shift, a zero
   divisor) taking the generic body without counting, an allocation
   failure raising. */
#define KOS_ELECTOR_RESULT_HELD(FAST, a, b, reuse, reuse2, out, RELEASE) \
  do {                                                                  \
    int kos_ir_rc_ = FAST((a), (b), (reuse), (reuse2), &(out));         \
    if (unlikely(kos_ir_rc_ <= 0)) {                                    \
      RELEASE;                                                          \
      if (kos_ir_rc_ < 0)                                               \
        RESULT_RAISE_AT(ctx, insn);                                     \
      TAILCALL(kos_elector_generic_of(insn), ctx, insn, value);         \
    }                                                                   \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
  } while (0)

/* The common form: nothing is held across the arithmetic. RELEASE in
   the held form runs before a decline or a raise leaves the body. */
#define KOS_ELECTOR_RESULT(FAST, a, b, reuse, reuse2, out)              \
  KOS_ELECTOR_RESULT_HELD(FAST, a, b, reuse, reuse2, out, ((void) 0))


/* One body per operator and kind for the two families whose operands
   are the top pair of the stack, OBJECT_OPERAND and OBJECT_INPLACE
   (int OP= int is value-identical to int OP int). The guard is the
   kind's check on the peeked pair; a miss counts. Both operands are
   offered as the result's home, the left first: when the stack is the
   only owner of one of them the result is written into it and the other
   alone is dropped; else the result replaces the pair. */
#define KOS_PAIR_ELECTOR(FAMILY, KIND, kind, NAME, OPERAND)             \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);            \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    KOS_ELECTOR_PAIR_SAFETYNET(insn);                                   \
    if (unlikely(! args))                                               \
      RESULT_RAISE_AT(ctx, insn);                                       \
    if (unlikely(! KOS_##KIND##_GUARD(args[0], args[1])))               \
      TAILCALL(el_miss, ctx, insn, value);                              \
    KOS_##KIND##_EXTRACT(args[0], args[1], kos_elector_generic_of(insn)); \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b,                   \
                       args[0], args[1], out);                          \
    if (out == args[0]) {                                               \
      kos_stack_drop1_fast(bl);                                         \
    } else if (out == args[1]) {                                        \
      /* the result lives in the right operand: it moves down over the \
         left, whose reference the settle leaves to us */               \
      PyObject *left = args[0];                                         \
      kos_stack_settle2_fast(bl, NULL);                                 \
      Py_DECREF(left);                                                  \
    } else {                                                            \
      kos_stack_replace2_fast(bl, out);                                 \
    }                                                                   \
    RESULT_NEXT(ctx);                                                   \
  }


/* One body per operator and kind for a family whose accumulator is a
   fast slot: the guard is the kind's check on the peeked slot value
   and, when RHS_GUARDED, on the operand the family supplies (RHS,
   borrowed; a pool constant's type was fixed at the election). The
   accumulator is offered as the result's home when its holder is a
   raw slot or a plain ref and that holder is its only owner: the
   result is written into it and no store follows. A constant ref's
   value is never written, since its store refuses; the result is
   stolen into the slot through the ref, as it is into a raw slot the
   value is shared with. Either way the family's operand release
   (DROP) runs first. A slot the peek cannot read (empty, or a ref
   kind that needs the general deref) is a miss; the generic body
   raises or resolves it. */
#define KOS_SLOT_ELECTOR(FAMILY, KIND, kind, NAME, RHS, RHS_GUARDED,    \
                         EXTRACT, DROP)                                 \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    size_t idx = (size_t) KosExtra_IMM(row, 1);                         \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    PyObject *acc;                                                      \
    PyObject *rhs;                                                      \
    int home;                                                           \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    acc = kos_ns_fast_peek_home(ns, idx, &home);                        \
    rhs = (RHS);                                                        \
    if (unlikely(! rhs))                                                \
      RESULT_RAISE_AT(ctx, insn);                                       \
    if (unlikely(! acc || ! KOS_##KIND##_CHECK(acc)                     \
                 || (RHS_GUARDED && ! KOS_##KIND##_CHECK(rhs))))        \
      TAILCALL(el_miss, ctx, insn, value);                              \
    EXTRACT(acc, rhs, kos_elector_generic_of(insn));                    \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b,                   \
                       home ? acc : NULL, RHS_GUARDED ? rhs : NULL, out); \
    if (RHS_GUARDED && out == rhs) {                                    \
      /* the result lives in the stack top: its reference moves to the \
         slot below instead of being dropped */                         \
      (*KosContext_BLOCK(ctx))->len -= 1;                               \
    } else {                                                            \
      DROP;                                                             \
    }                                                                   \
    if (out != acc && unlikely(kos_ns_fast_store_steal(ns, idx, out) < 0)) \
      RESULT_RAISE_AT(ctx, insn);                                       \
    RESULT_NEXT(ctx);                                                   \
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
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    size_t count = KosOpCount_COUNT(KosOp_ARG(insn));                   \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, count);        \
    KOS_##KIND##_VARS;                                                  \
    KOS_##KIND##_TYPE res;                                              \
    PyObject *out;                                                      \
    int rc;                                                             \
                                                                        \
    if (unlikely(! args))                                               \
      RESULT_RAISE_AT(ctx, insn);                                       \
    for (size_t i = 0; i < count; i++) {                                \
      if (unlikely(! KOS_##KIND##_CHECK(args[i])))                      \
        TAILCALL(el_miss, ctx, insn, value);                            \
    }                                                                   \
    KOS_##KIND##_EXTRACT(args[0], args[1], kos_elector_generic_of(insn)); \
    if (unlikely(! KOS_##KIND##_STEP(NAME)(a, b, &res)))                \
      TAILCALL(kos_elector_generic_of(insn), ctx, insn, value);         \
    for (size_t i = 2; i < count; i++) {                                \
      a = res;                                                          \
      KOS_##KIND##_EXTRACT_ONE(args[i], b, kos_elector_generic_of(insn)); \
      if (unlikely(! KOS_##KIND##_STEP(NAME)(a, b, &res)))              \
        TAILCALL(kos_elector_generic_of(insn), ctx, insn, value);       \
    }                                                                   \
                                                                        \
    rc = KOS_##KIND##_MATERIALISE(res, args[0], NULL, &out);            \
    if (unlikely(rc < 0))                                               \
      RESULT_RAISE_AT(ctx, insn);                                       \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    if (out == args[0]) {                                               \
      kos_stack_drop_noerr_fast(bl, count - 1);                         \
    } else {                                                            \
      kos_stack_drop_noerr_fast(bl, count);                             \
      if (unlikely(kos_stack_push_steal_fast(bl, out) < 0))             \
        RESULT_RAISE_AT(ctx, insn);                                     \
    }                                                                   \
    RESULT_NEXT(ctx);                                                   \
  }

KOS_NUMERIC_FOLD_ELECTORS(KOS_PAIR_FOLD_ELECTOR, pair_fold)


/* Replace the stack top with an owned value. */
static inline void kos_elector_replace_top(KosSegment **bl, PyObject *val) {
  KosSegment *s = *bl;
  PyObject *old = s->objs[s->len - 1];
  s->objs[s->len - 1] = val;
  Py_DECREF(old);
}


/* The top of the stack, borrowed, or NULL with an error set. */
KOS_OP_INLINE PyObject *kos_stack_peek_top(KosContext *ctx) {
  PyObject *const *top = kos_stack_tail_ptr_fast(*KosContext_BLOCK(ctx), 1);
  return top ? top[0] : NULL;
}

/* FAST_INPLACE: the operand is the top of the stack, borrowed for the
   arithmetic and dropped before the store. */
#define KOS_FAST_INPLACE_BODY(FAMILY, KIND, kind, NAME, OPERAND)        \
  KOS_SLOT_ELECTOR(                                                     \
      FAMILY, KIND, kind, NAME,                                         \
      kos_stack_peek_top(ctx), 1,                                       \
      KOS_##KIND##_EXTRACT,                                             \
      kos_stack_drop1_fast(KosContext_BLOCK(ctx)))

KOS_NUMERIC_ELECTORS(KOS_FAST_INPLACE_BODY, fast_inplace)


/* FAST_INPLACE_CONST: the operand is the pool constant on the row,
   borrowed; nothing to drop. Its type was checked at the election and
   cannot change, so the body does not guard it, and an int constant's
   C value was read into the slot then. */
#define KOS_FAST_INPLACE_CONST_BODY(FAMILY, KIND, kind, NAME, OPERAND)  \
  KOS_SLOT_ELECTOR(                                                     \
      FAMILY, KIND, kind, NAME,                                         \
      KosExtra_OBJ(row, 2), 0,                                          \
      KOS_##KIND##_EXTRACT_CONST,                                       \
      ((void) 0))

KOS_NUMERIC_ELECTORS(KOS_FAST_INPLACE_CONST_BODY, fast_inplace_const)


/* OBJECT_OPERAND_CONST: the left operand is the top of the stack, the
   right the pool constant on the row, borrowed. The constant's kind was
   checked at the election and cannot change, so only the top is
   guarded, and an int constant's C value was read into the slot then.
   The top is offered as the result's home, as the pair body offers its
   left operand; else the result replaces it. */
#define KOS_OPERAND_CONST_BODY(FAMILY, KIND, kind, NAME, OPERAND)       \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    PyObject *const *top = kos_stack_tail_ptr_fast(*bl, 1);             \
    PyObject *rhs = KosExtra_OBJ(row, 1);                               \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    if (unlikely(! top))                                                \
      RESULT_RAISE_AT(ctx, insn);                                       \
    if (unlikely(! KOS_##KIND##_CHECK(top[0])))                         \
      TAILCALL(el_miss, ctx, insn, value);                              \
    KOS_##KIND##_EXTRACT_CONST(top[0], rhs, kos_elector_generic_of(insn)); \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, top[0], NULL, out); \
    if (out != top[0])                                                  \
      kos_elector_replace_top(bl, out);                                 \
    RESULT_NEXT(ctx);                                                   \
  }

KOS_NUMERIC_ELECTORS(KOS_OPERAND_CONST_BODY, operand_const)


/* FAST_OPERAND_CONST: the left operand is the fast slot's value,
   peeked borrowed (a slot the peek cannot read misses; the generic
   body derefs and raises), the right the pool constant, borrowed,
   unguarded and read from the slot as above. The slot keeps its value,
   so no home is offered and the result is a fresh push. */
#define KOS_FAST_OPERAND_CONST_BODY(FAMILY, KIND, kind, NAME, OPERAND)  \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    PyObject *lhs;                                                      \
    PyObject *rhs = KosExtra_OBJ(row, 2);                               \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    lhs = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 1));          \
    if (unlikely(! lhs || ! KOS_##KIND##_CHECK(lhs)))                   \
      TAILCALL(el_miss, ctx, insn, value);                              \
    KOS_##KIND##_EXTRACT_CONST(lhs, rhs, kos_elector_generic_of(insn)); \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, NULL, NULL, out); \
    if (unlikely(kos_stack_push_steal_fast(KosContext_BLOCK(ctx), out) < 0)) \
      RESULT_RAISE_AT(ctx, insn);                                       \
    RESULT_NEXT(ctx);                                                   \
  }

KOS_NUMERIC_ELECTORS(KOS_FAST_OPERAND_CONST_BODY, fast_operand_const)


/* OBJECT_OPERAND_FAST: the left operand is the top of the stack, the
   right the fast slot's value, peeked borrowed (a slot the peek cannot
   read misses; the generic body derefs and raises). Both are guarded.
   The top is offered as the result's home, as the pair body offers
   its left operand; the slot keeps its value. */
#define KOS_OPERAND_FAST_BODY(FAMILY, KIND, kind, NAME, OPERAND)        \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    PyObject *const *top = kos_stack_tail_ptr_fast(*bl, 1);             \
    PyObject *rhs;                                                      \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    if (unlikely(! top))                                                \
      RESULT_RAISE_AT(ctx, insn);                                       \
    rhs = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 1));          \
    if (unlikely(! rhs || ! KOS_##KIND##_GUARD(top[0], rhs)))           \
      TAILCALL(el_miss, ctx, insn, value);                              \
    KOS_##KIND##_EXTRACT(top[0], rhs, kos_elector_generic_of(insn));    \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, top[0], NULL, out); \
    if (out != top[0])                                                  \
      kos_elector_replace_top(bl, out);                                 \
    RESULT_NEXT(ctx);                                                   \
  }

KOS_NUMERIC_ELECTORS(KOS_OPERAND_FAST_BODY, operand_fast)


/* FAST_OPERAND_FAST: both operands are fast slot values, peeked
   borrowed and guarded. The slots keep their values, so no home is
   offered and the result is a fresh push. */
#define KOS_FAST_OPERAND_FAST_BODY(FAMILY, KIND, kind, NAME, OPERAND)   \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    PyObject *lhs, *rhs;                                                \
    KOS_##KIND##_VARS;                                                  \
    PyObject *out;                                                      \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    lhs = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 1));          \
    rhs = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 2));          \
    if (unlikely(! lhs || ! rhs || ! KOS_##KIND##_GUARD(lhs, rhs)))     \
      TAILCALL(el_miss, ctx, insn, value);                              \
    KOS_##KIND##_EXTRACT(lhs, rhs, kos_elector_generic_of(insn));       \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, NULL, NULL, out); \
    if (unlikely(kos_stack_push_steal_fast(KosContext_BLOCK(ctx), out) < 0)) \
      RESULT_RAISE_AT(ctx, insn);                                       \
    RESULT_NEXT(ctx);                                                   \
  }

KOS_NUMERIC_ELECTORS(KOS_FAST_OPERAND_FAST_BODY, fast_operand_fast)


/* === FAST_ITEM_CONST family === */


/* obj[k] with obj in a fast slot and k a pool constant. A site whose
   constant is an exact int within a C long long elects a list or tuple
   body carrying the C index in the slot, so the body is the container
   guard, a length-normalised bounds check, and the load; a dict body
   looks the constant up; the generic body is the object protocol. An
   index outside the container declines to the protocol, which raises
   the IndexError through the object's own path. */


/**
   FAST_ITEM_CONST without an election: the object protocol over the
   slot's value and the constant, with obj held across the call as the
   item protocol holds it.
*/
KOS_ELECTOR(fast_item_const_generic) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  PyObject *obj;
  PyObject *out;

  KOS_OP_REQUIRE_NS(ns);
  kos_elector_generic_tick(KosOp_SLOT(insn));
  obj = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, 0));
  if (unlikely(! obj))
    RESULT_RAISE();
  out = PyObject_GetItem(obj, KosExtra_OBJ(row, 1));
  Py_DECREF(obj);
  if (unlikely(! out))
    RESULT_RAISE();
  RESULT_STEAL(ctx, out);
}


#define KOS_FAST_ITEM_CONST_SEQ_BODY(NAME, CHECK, SIZE, GET)             \
  KOS_ELECTOR(fast_item_const_##NAME) {                                 \
    KOS_SYNC_PC(ctx, insn);                                             \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    PyObject *obj;                                                      \
    Py_ssize_t i, size;                                                 \
    PyObject *out;                                                      \
                                                                        \
    KOS_OP_REQUIRE_NS(ns);                                              \
    obj = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 0));          \
    if (unlikely(! obj || ! CHECK(obj)))                                \
      TAILCALL(el_miss, ctx, insn, value);                              \
    i = KosOp_SLOT(insn)->u.elector.index;                              \
    size = SIZE(obj);                                                   \
    if (i < 0)                                                          \
      i += size;                                                        \
    if (unlikely((size_t) i >= (size_t) size))                          \
      TAILCALL(el_fast_item_const_generic, ctx, insn, value);           \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    out = GET(obj, i);                                                  \
    Py_INCREF(out);                                                     \
    RESULT_STEAL(ctx, out);                                             \
  }

KOS_FAST_ITEM_CONST_SEQ_BODY(list, PyList_CheckExact, PyList_GET_SIZE,
                             PyList_GET_ITEM)
KOS_FAST_ITEM_CONST_SEQ_BODY(tuple, PyTuple_CheckExact, PyTuple_GET_SIZE,
                             PyTuple_GET_ITEM)


/* The KeyError an exact dict raises for a missing key: the key as the
   one argument, wrapped so a tuple key reports as one key. */
static void kos_elector_set_key_error(PyObject *key) {
  PyObject *args = PyTuple_Pack(1, key);
  if (! args)
    return;
  PyErr_SetObject(PyExc_KeyError, args);
  Py_DECREF(args);
}


/* The dict body holds obj across the lookup as the item body does: a
   key comparison inside the dict can run user code. A missing key is
   the exact dict's KeyError, raised here rather than by a second
   lookup through the protocol. */
KOS_ELECTOR(fast_item_const_dict) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  PyObject *obj;
  PyObject *v;

  KOS_OP_REQUIRE_NS(ns);
  obj = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 0));
  if (unlikely(! obj || ! PyDict_CheckExact(obj)))
    TAILCALL(el_miss, ctx, insn, value);
  Py_INCREF(obj);
  v = PyDict_GetItemWithError(obj, KosExtra_OBJ(row, 1));
  if (unlikely(! v)) {
    Py_DECREF(obj);
    if (! PyErr_Occurred())
      kos_elector_set_key_error(KosExtra_OBJ(row, 1));
    RESULT_RAISE();
  }
  Py_INCREF(v);
  Py_DECREF(obj);
  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
  RESULT_STEAL(ctx, v);
}


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
  KOS_SYNC_PC(ctx, insn);
  KosSegment **bl = KosContext_BLOCK(ctx);
  PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);
  PyObject *out;

  KOS_ELECTOR_PAIR_SAFETYNET(insn);
  if (unlikely(! args))
    RESULT_RAISE();
  if (unlikely(! (PyUnicode_CheckExact(args[0])
                  && PyUnicode_CheckExact(args[1]))))
    TAILCALL(el_miss, ctx, insn, value);

  if (unlikely(str_fast_append(args[0], args[1], &out) < 0)) {
    kos_stack_settle2_fast(bl, NULL);
    RESULT_RAISE();
  }
  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
  kos_stack_settle2_fast(bl, out);
  RESULT_NEXT(ctx);
}


/* The join body: every operand of the run an exact str, or a miss;
   one allocation replaces the run. */
KOS_ELECTOR(pair_str_join) {
  KOS_SYNC_PC(ctx, insn);
  KosSegment **bl = KosContext_BLOCK(ctx);
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
    TAILCALL(el_miss, ctx, insn, value);
  }
  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
  kos_stack_drop_noerr_fast(bl, count);
  if (unlikely(kos_stack_push_steal_fast(bl, out) < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


/* The str body of a family whose accumulator is a fast slot: a raw
   slot's value is taken, the slot left empty until the store, so the
   append can grow it in place; a ref slot keeps its reference and the
   append concatenates. */
#define KOS_SLOT_STR_ELECTOR(FAMILY, RHS, RHS_GUARDED, DROP)            \
  KOS_ELECTOR(FAMILY##_str_add) {                                       \
    KOS_SYNC_PC(ctx, insn);                                             \
    const KosExtra *row = KosOp_ROW(insn);                              \
    size_t idx = (size_t) KosExtra_IMM(row, 1);                         \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
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
      TAILCALL(el_miss, ctx, insn, value);                              \
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
    RESULT_NEXT(ctx);                                                   \
  }

KOS_SLOT_STR_ELECTOR(
    fast_inplace,
    kos_stack_peek_top(ctx), 1,
    kos_stack_drop1_fast(KosContext_BLOCK(ctx)))
KOS_SLOT_STR_ELECTOR(
    fast_inplace_const,
    KosExtra_OBJ(row, 2), 0,
    ((void) 0))


/* === Compare families === */


/**
   CHECK_COMPARE without an election: chain-compare the count operands
   under the selector, negated when the row's negate field is set,
   drop them, and jump on failure.
*/
KOS_ELECTOR(check_compare_generic) {
  KOS_SYNC_PC(ctx, insn);
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

  args = KosContext_StackTailPtr(ctx, count);
  if (unlikely(! args))
    RESULT_RAISE();

  res = kos_compare_chain((KosCompare) KosExtra_IMM(row, 0), args, count);
  KosContext_StackDropNoErr(ctx, count);
  if (unlikely(res < 0))
    RESULT_RAISE();
  if ((res != 0) == (int) KosExtra_IMM(row, 3))
    RESULT_JUMP_AT(ctx, KosExtra_TARGET(row, 2));
  RESULT_NEXT(ctx);
}


/**
   OBJECT_COMPARE without an election: chain-compare the count operands
   under the selector and push the result.
*/
KOS_ELECTOR(object_compare_generic) {
  KOS_SYNC_PC(ctx, insn);
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

  args = KosContext_StackTailPtr(ctx, count);
  if (unlikely(! args))
    RESULT_RAISE();

  result = kos_compare_reduce((KosCompare) KosCmpCount_SEL(arg), args, count);
  KosContext_StackDropNoErr(ctx, count);
  if (unlikely(! result))
    RESULT_RAISE();
  RESULT_STEAL(ctx, result);
}


/* One body per kind for each compare opcode: the stack pair under a
   richcompare selector, settled on the kind's compare over the
   extracted values. CHECK_COMPARE takes its selector from the row,
   drops the pair, and jumps when the compare, negated under a set
   negate field, fails; OBJECT_COMPARE takes it from the packed
   argument and replaces the pair with the bool. SEL reads the selector
   after the family's prologue (ROW) has run; SETTLE takes the
   compare's result. */
#define KOS_COMPARE_ELECTOR(FAMILY, KIND, kind, ROW, SEL, SETTLE)       \
  KOS_ELECTOR(FAMILY##_##kind) {                                        \
    ROW;                                                                \
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);            \
    KOS_##KIND##_VARS;                                                  \
    int held;                                                           \
                                                                        \
    if (unlikely(! args))                                               \
      RESULT_RAISE_AT(ctx, insn);                                       \
    if (unlikely(! KOS_##KIND##_GUARD(args[0], args[1])))               \
      TAILCALL(el_miss, ctx, insn, value);                              \
    KOS_##KIND##_EXTRACT(args[0], args[1], el_##FAMILY##_generic);      \
                                                                        \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    held = KOS_##KIND##_COMPARE((KosCompare) (SEL), a, b);              \
    SETTLE(held);                                                       \
  }

#define KOS_CHECK_COMPARE_SETTLE(held)                                  \
  do {                                                                  \
    kos_stack_drop_noerr_fast(bl, 2);                                   \
    if ((held) != (int) KosExtra_IMM(row, 3))                           \
      RESULT_NEXT(ctx);                                                 \
    RESULT_JUMP_AT(ctx, KosExtra_TARGET(row, 2));                       \
  } while (0)

#define KOS_OBJECT_COMPARE_SETTLE(held)                                 \
  do {                                                                  \
    kos_stack_replace2_fast(bl, PyBool_FromLong(held));                 \
    RESULT_NEXT(ctx);                                                   \
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
    KosContext *ctx,
    KosCompare sel,
    size_t count,
    KosThreadFn int_body,
    KosThreadFn float_body) {

  KosSegment *s = *KosContext_BLOCK(ctx);
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
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  return kos_elector_compare_classify(
      ctx, (KosCompare) KosExtra_IMM(row, 0), (size_t) KosExtra_IMM(row, 1),
      el_check_compare_int, el_check_compare_float);
}


static inline KosThreadFn kos_elector_object_compare_classify(
    KosContext *ctx,
    void *insn) {

  uint16_t arg = KosOp_ARG(insn);
  return kos_elector_compare_classify(
      ctx, (KosCompare) KosCmpCount_SEL(arg), (size_t) KosCmpCount_COUNT(arg),
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
   unset location, so a non-NULL address holds a value. On 3.13+ a
   materialised dict shares still-valid values, and a store into them
   must notify that dict's watchers, so its presence is checked too.
   The type guard the head re-checks is what makes the address valid:
   a type with generic getattr and setattr and no descriptor over the
   name, so a store through the address is what setattr would do. */
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
#if PY_MINOR_VERSION >= 13
    if (kos_managed_dict_peek(obj))
      return NULL;
#endif
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
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(KosExtra_IMM(row, 0));
  int count = (int) KosOpCount_COUNT(KosExtra_IMM(row, 0));
  size_t idx = (size_t) KosExtra_IMM(row, 1);
  PyObject *attr = KosExtra_OBJ(row, 2);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  KosCacheSlot *slot = KosOp_SLOT(insn);
  PyObject *obj;
  PyObject *const *args;
  int res;

  KOS_OP_REQUIRE_NS(ns);
  kos_elector_generic_tick(slot ? slot + 1 : NULL);

  obj = kos_ns_fast_deref(ns, idx);
  if (! obj)
    RESULT_RAISE();

  args = count ? KosContext_StackTailPtr(ctx, (size_t) count) : NULL;
  if (count && ! args) {
    Py_DECREF(obj);
    RESULT_RAISE();
  }

  res = kos_attr_inplace_cached(slot, obj, attr, op, args, count);
  KosContext_StackDropNoErr(ctx, (size_t) count);
  Py_DECREF(obj);
  if (unlikely(res < 0))
    RESULT_RAISE();
  RESULT_NEXT(ctx);
}


/* The tail of an attribute body once the result is in hand: the one
   operand dropped, then the result, owned, swapped into the location
   unless it was written in place. Nothing runs between the head's
   load and this store, so the location is the one the head read. */
#define KOS_ATTR_ELECTOR_STORE(addr, acc, out)                          \
  do {                                                                  \
    kos_stack_drop1_fast(KosContext_BLOCK(ctx));                        \
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
    PyObject *kos_ah_ns_ = KosContext_NAMESPACE(ctx);                   \
    PyObject *kos_ah_obj_;                                              \
    KOS_OP_REQUIRE_NS(kos_ah_ns_);                                      \
    kos_ah_obj_ = kos_ns_fast_peek(kos_ah_ns_,                          \
                                   (size_t) KosExtra_IMM(row, 1));      \
    (rhs) = kos_stack_peek_top(ctx);                                    \
    if (unlikely(! (rhs)))                                              \
      RESULT_RAISE();                                                   \
    if (unlikely(! kos_ah_obj_ || kos_ah_field_->kind != KOS_CACHE_FIELD \
                 || ! field_guard_ok(Py_TYPE(kos_ah_obj_), kos_ah_field_))) \
      TAILCALL(el_miss, ctx, insn, value);                              \
    (addr) = kos_field_direct_addr(kos_ah_obj_, kos_ah_field_->u.field.mode, \
                                   kos_ah_field_->u.field.index);       \
    if (unlikely(! (addr)))                                             \
      TAILCALL(el_miss, ctx, insn, value);                              \
    (acc) = *(addr);                                                    \
  } while (0)


/* One body per operator and kind for an attribute site: the head,
   the kind's guard on the accumulator and the operand, the extraction
   declining to the family's generic body, the accumulator offered as
   the result's home, the store. */
#define KOS_ATTR_ELECTOR(FAMILY, KIND, kind, NAME, OPERAND)             \
  KOS_ELECTOR(FAMILY##_##kind##_##NAME) {                               \
    KOS_SYNC_PC(ctx, insn);                                             \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *rhs, *acc, *out;                                          \
    PyObject **addr;                                                    \
    KOS_##KIND##_VARS;                                                  \
                                                                        \
    KOS_ATTR_ELECTOR_HEAD(rhs, addr, acc);                              \
    if (unlikely(! KOS_##KIND##_GUARD(acc, rhs)))                       \
      TAILCALL(el_miss, ctx, insn, value);                              \
    KOS_##KIND##_EXTRACT(acc, rhs, el_##FAMILY##_generic);              \
                                                                        \
    KOS_ELECTOR_RESULT(KOS_##KIND##_FAST(NAME), a, b, acc, NULL, out);  \
    KOS_ATTR_ELECTOR_STORE(addr, acc, out);                             \
    RESULT_NEXT(ctx);                                                   \
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
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);
  PyObject *rhs, *acc, *out;
  PyObject **addr;

  KOS_ATTR_ELECTOR_HEAD(rhs, addr, acc);
  if (unlikely(! PyUnicode_CheckExact(acc) || ! PyUnicode_CheckExact(rhs)))
    TAILCALL(el_miss, ctx, insn, value);

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
  kos_stack_drop1_fast(KosContext_BLOCK(ctx));
  RESULT_NEXT(ctx);
}


/* The classification of a FAST_ATTR_INPLACE site: one operand, the
   field slot resolved to a direct location for the receiver's type,
   and the accumulator and operand an exact int, float, or str pair. */
static inline KosThreadFn kos_elector_fast_attr_inplace_classify(
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  KosCacheSlot *field = KosOp_SLOT(insn);
  KosSegment *s = *KosContext_BLOCK(ctx);
  PyObject *obj = kos_ns_fast_peek(
      KosContext_NAMESPACE(ctx), (size_t) KosExtra_IMM(row, 1));
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
  KOS_SYNC_PC(ctx, insn);
  KosCacheSlot *slot = KosOp_SLOT(insn);
  KosThreadFn fn = kos_elector_fast_attr_inplace_classify(ctx, insn);

  if (! fn)
    fn = el_fast_attr_inplace_generic;
  kos_elector_elect(slot ? slot + 1 : NULL, fn);
  TAILCALL(fn, ctx, insn, value);
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
    KosContext *ctx,
    void *insn) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  size_t count = KosOpCount_COUNT(arg);
  KosSegment *s = *KosContext_BLOCK(ctx);
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
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  KosSegment *s = *KosContext_BLOCK(ctx);
  PyObject *acc = kos_ns_fast_peek(
      KosContext_NAMESPACE(ctx), (size_t) KosExtra_IMM(row, 1));
  PyObject *rhs = (s && s->len >= 1) ? kos_stack_tail_ptr_fast(s, 1)[0] : NULL;

  return kos_elector_num_body(
      (KosOperand) KosExtra_IMM(row, 0), acc, rhs,
      kos_fast_inplace_int_bodies, kos_fast_inplace_float_bodies,
      el_fast_inplace_str_add);
}


/* The classification of a FAST_INPLACE_CONST site: the peeked slot
   value and the pool constant. */
static inline KosThreadFn kos_elector_fast_inplace_const_classify(
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *acc = kos_ns_fast_peek(
      KosContext_NAMESPACE(ctx), (size_t) KosExtra_IMM(row, 1));
  PyObject *rhs = KosExtra_OBJ(row, 2);
  long long b;

  /* a constant the int body could never read would decline on every
     execution; the site stays generic. One it can read is kept in the
     slot, which the election leaves as it finds it. */
  if (PyLong_CheckExact(rhs)) {
    if (! kos_long_as_ll(rhs, &b))
      return NULL;
    kos_elector_keep_const(KosOp_SLOT(insn), b);
  }
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
    KOS_SYNC_PC(ctx, insn);                                             \
    KosThreadFn fn = CLASSIFY(ctx, insn);                               \
                                                                        \
    if (! fn)                                                           \
      fn = el_##FAMILY##_generic;                                       \
    kos_elector_elect(KosOp_SLOT(insn), fn);                            \
    TAILCALL(fn, ctx, insn, value);                                     \
  }

/* The classification of an OBJECT_OPERAND_CONST site: the top of the
   stack and the pool constant. A constant the int body could never
   read would decline on every execution; the site stays generic. */
static inline KosThreadFn kos_elector_operand_const_classify(
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *lhs = kos_stack_peek_top(ctx);
  PyObject *rhs = KosExtra_OBJ(row, 1);
  long long b;

  if (! lhs) {
    PyErr_Clear();
    return NULL;
  }
  if (PyLong_CheckExact(rhs)) {
    if (! kos_long_as_ll(rhs, &b))
      return NULL;
    kos_elector_keep_const(KosOp_SLOT(insn), b);
  }
  return kos_elector_num_body(
      (KosOperand) KosExtra_IMM(row, 0), lhs, rhs,
      kos_operand_const_int_bodies, kos_operand_const_float_bodies, NULL);
}


/* The classification of a FAST_OPERAND_CONST site: the peeked slot
   value and the pool constant, with the same rule on the constant. */
static inline KosThreadFn kos_elector_fast_operand_const_classify(
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *lhs = kos_ns_fast_peek(
      KosContext_NAMESPACE(ctx), (size_t) KosExtra_IMM(row, 1));
  PyObject *rhs = KosExtra_OBJ(row, 2);
  long long b;

  if (PyLong_CheckExact(rhs)) {
    if (! kos_long_as_ll(rhs, &b))
      return NULL;
    kos_elector_keep_const(KosOp_SLOT(insn), b);
  }
  return kos_elector_num_body(
      (KosOperand) KosExtra_IMM(row, 0), lhs, rhs,
      kos_fast_operand_const_int_bodies, kos_fast_operand_const_float_bodies,
      NULL);
}


KOS_ELECTOR_ELECT(operand, kos_elector_pair_classify)
KOS_ELECTOR_ELECT(inplace, kos_elector_pair_classify)
KOS_ELECTOR_ELECT(fast_inplace, kos_elector_fast_inplace_classify)
KOS_ELECTOR_ELECT(fast_inplace_const, kos_elector_fast_inplace_const_classify)
/* The classification of a FAST_ITEM_CONST site: the peeked slot value
   and the pool constant. A dict elects the dict body; a list or tuple
   under an exact-int constant within a C long long elects the sequence
   body with the index written into the slot; anything else, and a site
   with no slot to hold the index, stays generic. */
static inline KosThreadFn kos_elector_fast_item_const_classify(
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  KosCacheSlot *slot = KosOp_SLOT(insn);
  PyObject *obj = kos_ns_fast_peek(
      KosContext_NAMESPACE(ctx), (size_t) KosExtra_IMM(row, 0));
  PyObject *key = KosExtra_OBJ(row, 1);
  long long i;

  if (! obj || ! slot)
    return NULL;
  if (PyDict_CheckExact(obj))
    return el_fast_item_const_dict;
  if (! PyLong_CheckExact(key) || ! kos_long_as_ll(key, &i))
    return NULL;
  /* the slot holds a Py_ssize_t: a constant beyond it stays generic so
     it raises IndexError instead of wrapping (32-bit Py_ssize_t) */
  if (i < (long long) PY_SSIZE_T_MIN || i > (long long) PY_SSIZE_T_MAX)
    return NULL;
  if (PyList_CheckExact(obj)) {
    slot->u.elector.index = (Py_ssize_t) i;
    return el_fast_item_const_list;
  }
  if (PyTuple_CheckExact(obj)) {
    slot->u.elector.index = (Py_ssize_t) i;
    return el_fast_item_const_tuple;
  }
  return NULL;
}


/* The classification of an OBJECT_OPERAND_FAST site: the top of the
   stack and the peeked slot value. */
static inline KosThreadFn kos_elector_operand_fast_classify(
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *lhs = kos_stack_peek_top(ctx);
  PyObject *rhs = kos_ns_fast_peek(
      KosContext_NAMESPACE(ctx), (size_t) KosExtra_IMM(row, 1));

  if (! lhs) {
    PyErr_Clear();
    return NULL;
  }
  return kos_elector_num_body(
      (KosOperand) KosExtra_IMM(row, 0), lhs, rhs,
      kos_operand_fast_int_bodies, kos_operand_fast_float_bodies, NULL);
}


/* The classification of a FAST_OPERAND_FAST site: the two peeked slot
   values. */
static inline KosThreadFn kos_elector_fast_operand_fast_classify(
    KosContext *ctx,
    void *insn) {

  const KosExtra *row = KosOp_ROW(insn);
  PyObject *ns = KosContext_NAMESPACE(ctx);
  PyObject *lhs = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 1));
  PyObject *rhs = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, 2));

  return kos_elector_num_body(
      (KosOperand) KosExtra_IMM(row, 0), lhs, rhs,
      kos_fast_operand_fast_int_bodies, kos_fast_operand_fast_float_bodies,
      NULL);
}


KOS_ELECTOR_ELECT(operand_const, kos_elector_operand_const_classify)
KOS_ELECTOR_ELECT(fast_operand_const, kos_elector_fast_operand_const_classify)
KOS_ELECTOR_ELECT(operand_fast, kos_elector_operand_fast_classify)
KOS_ELECTOR_ELECT(fast_operand_fast, kos_elector_fast_operand_fast_classify)
KOS_ELECTOR_ELECT(fast_item_const, kos_elector_fast_item_const_classify)
KOS_ELECTOR_ELECT(check_compare, kos_elector_check_compare_classify)
KOS_ELECTOR_ELECT(object_compare, kos_elector_object_compare_classify)


/* === The other constant-key item families === */


/* obj[k] and obj[k] = v with k a pool constant, for every read and
   store opcode with a cache slot: the object from a fast slot or the
   stack top, the stored value from the stack, the pool, or a fast
   slot. Each family elects a list body (a tuple body for a read) that
   reads the constant's C index off the slot, a dict body that looks
   the constant up, or the object protocol. A body reads its operands
   in place and adjusts the stack only once it has succeeded, so a
   guard miss or a decline hands the site to the generic body with
   every operand where it was. */


/* The site's index normalised against a sequence's length; 0 when the
   index is outside it, which declines to the protocol for the
   IndexError. */
static inline int kos_item_const_index(
    KosCacheSlot *slot, Py_ssize_t size, Py_ssize_t *out) {
  Py_ssize_t i = slot->u.elector.index;
  if (i < 0)
    i += size;
  if (unlikely((size_t) i >= (size_t) size))
    return 0;
  *out = i;
  return 1;
}


/* --- ITEM_CONST: the object on the stack top, the result in its place --- */

KOS_ELECTOR(item_const_generic) {
  KOS_SYNC_PC(ctx, insn);
  KosSegment **bl = KosContext_BLOCK(ctx);
  PyObject *const *tail = kos_stack_tail_ptr_fast(*bl, 1);
  PyObject *out;

  kos_elector_generic_tick(KosOp_SLOT(insn));
  if (unlikely(! tail))
    RESULT_RAISE();
  out = PyObject_GetItem(tail[0], KosOp_OBJ(insn));
  if (unlikely(! out))
    RESULT_RAISE();
  kos_elector_replace_top(bl, out);
  RESULT_NEXT(ctx);
}

#define KOS_ITEM_CONST_SEQ_BODY(NAME, CHECK, SIZE, GET)                 \
  KOS_ELECTOR(item_const_##NAME) {                                      \
    KOS_SYNC_PC(ctx, insn);                                             \
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    PyObject *const *tail = kos_stack_tail_ptr_fast(*bl, 1);            \
    Py_ssize_t i;                                                       \
    PyObject *out;                                                      \
                                                                        \
    if (unlikely(! tail))                                               \
      RESULT_RAISE();                                                   \
    if (unlikely(! CHECK(tail[0])))                                     \
      TAILCALL(el_miss, ctx, insn, value);                              \
    if (unlikely(! kos_item_const_index(KosOp_SLOT(insn),               \
                                        SIZE(tail[0]), &i)))            \
      TAILCALL(el_item_const_generic, ctx, insn, value);                \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    out = GET(tail[0], i);                                              \
    Py_INCREF(out);                                                     \
    kos_elector_replace_top(bl, out);                                   \
    RESULT_NEXT(ctx);                                                   \
  }

KOS_ITEM_CONST_SEQ_BODY(list, PyList_CheckExact, PyList_GET_SIZE,
                        PyList_GET_ITEM)
KOS_ITEM_CONST_SEQ_BODY(tuple, PyTuple_CheckExact, PyTuple_GET_SIZE,
                        PyTuple_GET_ITEM)

KOS_ELECTOR(item_const_dict) {
  KOS_SYNC_PC(ctx, insn);
  KosSegment **bl = KosContext_BLOCK(ctx);
  PyObject *const *tail = kos_stack_tail_ptr_fast(*bl, 1);
  PyObject *v;

  if (unlikely(! tail))
    RESULT_RAISE();
  if (unlikely(! PyDict_CheckExact(tail[0])))
    TAILCALL(el_miss, ctx, insn, value);
  /* the stack's reference holds the dict across the lookup; a missing
     key is the exact dict's KeyError, raised here */
  v = PyDict_GetItemWithError(tail[0], KosOp_OBJ(insn));
  if (unlikely(! v)) {
    if (! PyErr_Occurred())
      kos_elector_set_key_error(KosOp_OBJ(insn));
    RESULT_RAISE();
  }
  Py_INCREF(v);
  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);
  kos_elector_replace_top(bl, v);
  RESULT_NEXT(ctx);
}


/* --- The six store families --- */

/* A store body is made of: how the object is reached (OBJ: sets obj
   borrowed, or misses), how the value is reached (VAL: sets val
   owned, or raises), the key, and how the stack settles after a hit
   (SETTLE). The list core moves val into the cell and releases the
   displaced value; the dict core sets and releases val. A decline or
   a miss leaves val where VAL found it: the pool, a fast slot, or the
   stack, none of which the body has consumed yet, so the generic body
   starts clean. */

/* Each family names: FAMILY; NEED, the stack depth it reads; KEY, the
   constant; OBJ_STMT, obj borrowed or a miss; VAL_STMT, val borrowed
   from its source; SETTLE, the stack adjustment after the store took
   its own reference to val; and GENERIC_BODY, the protocol form. */
#define KOS_ITEM_CONST_STORE_BODIES(FAMILY, NEED, KEY, OBJ_STMT, VAL_STMT, \
                                    OBJ_HOLD, SETTLE)                   \
  KOS_ELECTOR(FAMILY##_list) {                                          \
    KOS_SYNC_PC(ctx, insn);                                             \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    PyObject *const *tail = kos_stack_tail_ptr_fast(*bl, NEED);         \
    PyObject *obj, *val, *old;                                          \
    Py_ssize_t i;                                                       \
    (void) row; (void) ns;                                              \
    if (unlikely(NEED && ! tail))                                       \
      RESULT_RAISE();                                                   \
    OBJ_STMT;                                                           \
    if (unlikely(! PyList_CheckExact(obj)))                             \
      TAILCALL(el_miss, ctx, insn, value);                              \
    if (unlikely(! kos_item_const_index(KosOp_SLOT(insn),               \
                                        PyList_GET_SIZE(obj), &i)))     \
      TAILCALL(el_##FAMILY##_generic, ctx, insn, value);                \
    VAL_STMT;                                                           \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    Py_INCREF(val);                                                     \
    old = PyList_GET_ITEM(obj, i);                                      \
    PyList_SET_ITEM(obj, i, val);                                       \
    Py_DECREF(old);                                                     \
    SETTLE;                                                             \
    RESULT_NEXT(ctx);                                                   \
  }                                                                     \
  KOS_ELECTOR(FAMILY##_dict) {                                          \
    KOS_SYNC_PC(ctx, insn);                                             \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    PyObject *const *tail = kos_stack_tail_ptr_fast(*bl, NEED);         \
    PyObject *obj, *val;                                                \
    int rc;                                                             \
    (void) row; (void) ns;                                              \
    if (unlikely(NEED && ! tail))                                       \
      RESULT_RAISE();                                                   \
    OBJ_STMT;                                                           \
    if (unlikely(! PyDict_CheckExact(obj)))                             \
      TAILCALL(el_miss, ctx, insn, value);                              \
    VAL_STMT;                                                           \
    /* the key lookup can run an existing key's __eq__, which can rebind \
       a fast slot: a dict borrowed from one is held. A value is not:   \
       the dict takes its own reference to it before the lookup on      \
       every supported version. */                                      \
    if (OBJ_HOLD)                                                       \
      Py_INCREF(obj);                                                   \
    rc = PyDict_SetItem(obj, KEY, val);                                 \
    if (OBJ_HOLD)                                                       \
      Py_DECREF(obj);                                                   \
    if (unlikely(rc < 0))                                               \
      RESULT_RAISE();                                                   \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    SETTLE;                                                             \
    RESULT_NEXT(ctx);                                                   \
  }

/* The generic body of a store family: the object protocol with the
   operands read the same way. A stack operand is held by the operand
   stack until SETTLE and a pool constant by the code object; only a
   fast-slot deref yields a reference of its own, released after. */
#define KOS_ITEM_CONST_STORE_GENERIC(FAMILY, NEED, KEY, OBJ_STMT,       \
                                     VAL_STMT, SETTLE)                  \
  KOS_ELECTOR(FAMILY##_generic) {                                       \
    KOS_SYNC_PC(ctx, insn);                                             \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    KosSegment **bl = KosContext_BLOCK(ctx);                            \
    PyObject *const *tail = kos_stack_tail_ptr_fast(*bl, NEED);         \
    PyObject *obj, *val;                                                \
    PyObject *obj_owned = NULL;  /* a deref's own reference, if any */    \
    PyObject *val_owned = NULL;                                         \
    int rc;                                                             \
    (void) row; (void) ns;                                              \
    kos_elector_generic_tick(KosOp_SLOT(insn));                         \
    if (unlikely(NEED && ! tail))                                       \
      RESULT_RAISE();                                                   \
    OBJ_STMT;                                                           \
    VAL_STMT;                                                           \
    rc = PyObject_SetItem(obj, KEY, val);                               \
    Py_XDECREF(val_owned);                                              \
    Py_XDECREF(obj_owned);                                              \
    if (unlikely(rc < 0))                                               \
      RESULT_RAISE();                                                   \
    SETTLE;                                                             \
    RESULT_NEXT(ctx);                                                   \
  }

/* obj: the stack top below the value (NEED 2) or the top (NEED 1) */
#define KOS_OBJ_STACK(n)          obj = tail[0]
/* obj: a fast slot, borrowed, or a miss; owned form derefs (raises) */
#define KOS_OBJ_FAST(k)                                                 \
  do { obj = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, k));       \
       if (unlikely(! obj)) TAILCALL(el_miss, ctx, insn, value); } while (0)
#define KOS_OBJ_FAST_OWNED(k)                                           \
  do { obj = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, k));      \
       if (unlikely(! obj)) RESULT_RAISE();                             \
       obj_owned = obj; } while (0)
/* val: the stack top, the pool, or a fast slot; the generic form of
   the fast slot derefs so an empty slot raises */
#define KOS_VAL_STACK             val = tail[NEED_ - 1]
#define KOS_VAL_CONST(k)          val = KosExtra_OBJ(row, k)
#define KOS_VAL_FAST(k)                                                 \
  do { val = kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, k));       \
       if (unlikely(! val)) TAILCALL(el_miss, ctx, insn, value); } while (0)
/* the deref may build the value (a computing ref), so its reference is
   the only one: it stays owned across the store and is released after */
#define KOS_VAL_FAST_OWNED(k)                                           \
  do { val = kos_ns_fast_deref(ns, (size_t) KosExtra_IMM(row, k));      \
       if (unlikely(! val)) { Py_XDECREF(obj_owned); RESULT_RAISE(); }   \
       val_owned = val; } while (0)

/* ITEM_CONST_STORE: stack [obj, val]; key is the op's const */
#define NEED_ 2
KOS_ITEM_CONST_STORE_GENERIC(item_const_store, 2, KosOp_OBJ(insn),
    KOS_OBJ_STACK(0), KOS_VAL_STACK,
    kos_stack_drop_noerr_fast(bl, 2))
KOS_ITEM_CONST_STORE_BODIES(item_const_store, 2, KosOp_OBJ(insn),
    KOS_OBJ_STACK(0), KOS_VAL_STACK, 0,
    kos_stack_drop_noerr_fast(bl, 2))
#undef NEED_

/* ITEM_CONST_STORE_CONST: stack [obj]; row: key, val */
#define NEED_ 1
KOS_ITEM_CONST_STORE_GENERIC(item_const_store_const, 1,
    KosExtra_OBJ(row, 0),
    KOS_OBJ_STACK(0), KOS_VAL_CONST(1),
    kos_stack_drop_noerr_fast(bl, 1))
KOS_ITEM_CONST_STORE_BODIES(item_const_store_const, 1,
    KosExtra_OBJ(row, 0),
    KOS_OBJ_STACK(0), KOS_VAL_CONST(1), 0,
    kos_stack_drop_noerr_fast(bl, 1))

/* ITEM_CONST_STORE_FAST: stack [obj]; row: key, val slot */
KOS_ITEM_CONST_STORE_GENERIC(item_const_store_fast, 1,
    KosExtra_OBJ(row, 0),
    KOS_OBJ_STACK(0), KOS_VAL_FAST_OWNED(1),
    kos_stack_drop_noerr_fast(bl, 1))
KOS_ITEM_CONST_STORE_BODIES(item_const_store_fast, 1,
    KosExtra_OBJ(row, 0),
    KOS_OBJ_STACK(0), KOS_VAL_FAST(1), 0,
    kos_stack_drop_noerr_fast(bl, 1))

/* FAST_ITEM_CONST_STORE: stack [val]; row: obj slot, key */
KOS_ITEM_CONST_STORE_GENERIC(fast_item_const_store, 1,
    KosExtra_OBJ(row, 1),
    KOS_OBJ_FAST_OWNED(0), KOS_VAL_STACK,
    kos_stack_drop_noerr_fast(bl, 1))
KOS_ITEM_CONST_STORE_BODIES(fast_item_const_store, 1,
    KosExtra_OBJ(row, 1),
    KOS_OBJ_FAST(0), KOS_VAL_STACK, 1,
    kos_stack_drop_noerr_fast(bl, 1))
#undef NEED_

/* FAST_ITEM_CONST_STORE_CONST: no stack; row: obj slot, key, val */
#define NEED_ 0
KOS_ITEM_CONST_STORE_GENERIC(fast_item_const_store_const, 0,
    KosExtra_OBJ(row, 1),
    KOS_OBJ_FAST_OWNED(0), KOS_VAL_CONST(2), ((void) 0))
KOS_ITEM_CONST_STORE_BODIES(fast_item_const_store_const, 0,
    KosExtra_OBJ(row, 1),
    KOS_OBJ_FAST(0), KOS_VAL_CONST(2), 1, ((void) 0))

/* FAST_ITEM_CONST_STORE_FAST: no stack; row: obj slot, key, val slot */
KOS_ITEM_CONST_STORE_GENERIC(fast_item_const_store_fast, 0,
    KosExtra_OBJ(row, 1),
    KOS_OBJ_FAST_OWNED(0), KOS_VAL_FAST_OWNED(2), ((void) 0))
KOS_ITEM_CONST_STORE_BODIES(fast_item_const_store_fast, 0,
    KosExtra_OBJ(row, 1),
    KOS_OBJ_FAST(0), KOS_VAL_FAST(2), 1, ((void) 0))
#undef NEED_


/* --- Classification --- */

/* A constant-key site's body for the object it sees: the dict body; a
   list (or, for a read, a tuple) under an exact-int constant within a
   C long long, with the index written into the slot; else NULL. */
static inline KosThreadFn kos_item_const_body(
    KosCacheSlot *slot, PyObject *obj, PyObject *key,
    KosThreadFn dict_body, KosThreadFn list_body, KosThreadFn tuple_body) {
  long long i;

  if (! obj || ! slot)
    return NULL;
  if (PyDict_CheckExact(obj))
    return dict_body;
  if (! PyLong_CheckExact(key) || ! kos_long_as_ll(key, &i))
    return NULL;
  /* the slot holds a Py_ssize_t: a constant beyond it stays generic so
     it raises IndexError instead of wrapping (32-bit Py_ssize_t) */
  if (i < (long long) PY_SSIZE_T_MIN || i > (long long) PY_SSIZE_T_MAX)
    return NULL;
  if (PyList_CheckExact(obj)) {
    slot->u.elector.index = (Py_ssize_t) i;
    return list_body;
  }
  if (tuple_body && PyTuple_CheckExact(obj)) {
    slot->u.elector.index = (Py_ssize_t) i;
    return tuple_body;
  }
  return NULL;
}

#define KOS_ITEM_CONST_CLASSIFY(FAMILY, OBJ_EXPR, KEY_EXPR, TUPLE)     \
  static inline KosThreadFn kos_elector_##FAMILY##_classify(            \
      KosContext *ctx, void *insn) {                                    \
    const KosExtra *row = KosOp_ROW(insn);                              \
    PyObject *ns = KosContext_NAMESPACE(ctx);                           \
    KosSegment *s = *KosContext_BLOCK(ctx);                             \
    PyObject *obj = (OBJ_EXPR);                                         \
    (void) row; (void) ns; (void) s;                                    \
    return kos_item_const_body(KosOp_SLOT(insn), obj, (KEY_EXPR),       \
                               el_##FAMILY##_dict, el_##FAMILY##_list,  \
                               TUPLE);                                  \
  }

/* the stack object at depth n (1 = top) or NULL */
#define KOS_STACK_AT(n) \
  ((s && s->len >= (size_t) (n)) ? kos_stack_tail_ptr_fast(s, (n))[0] : NULL)
#define KOS_FAST_AT(k)  kos_ns_fast_peek(ns, (size_t) KosExtra_IMM(row, k))

KOS_ITEM_CONST_CLASSIFY(item_const, KOS_STACK_AT(1), KosOp_OBJ(insn),
                        el_item_const_tuple)
KOS_ITEM_CONST_CLASSIFY(item_const_store, KOS_STACK_AT(2), KosOp_OBJ(insn),
                        NULL)
KOS_ITEM_CONST_CLASSIFY(item_const_store_const, KOS_STACK_AT(1),
                        KosExtra_OBJ(row, 0), NULL)
KOS_ITEM_CONST_CLASSIFY(item_const_store_fast, KOS_STACK_AT(1),
                        KosExtra_OBJ(row, 0), NULL)
KOS_ITEM_CONST_CLASSIFY(fast_item_const_store, KOS_FAST_AT(0),
                        KosExtra_OBJ(row, 1), NULL)
KOS_ITEM_CONST_CLASSIFY(fast_item_const_store_const, KOS_FAST_AT(0),
                        KosExtra_OBJ(row, 1), NULL)
KOS_ITEM_CONST_CLASSIFY(fast_item_const_store_fast, KOS_FAST_AT(0),
                        KosExtra_OBJ(row, 1), NULL)

KOS_ELECTOR_ELECT(item_const, kos_elector_item_const_classify)
KOS_ELECTOR_ELECT(item_const_store, kos_elector_item_const_store_classify)
KOS_ELECTOR_ELECT(item_const_store_const,
                  kos_elector_item_const_store_const_classify)
KOS_ELECTOR_ELECT(item_const_store_fast,
                  kos_elector_item_const_store_fast_classify)
KOS_ELECTOR_ELECT(fast_item_const_store,
                  kos_elector_fast_item_const_store_classify)
KOS_ELECTOR_ELECT(fast_item_const_store_const,
                  kos_elector_fast_item_const_store_const_classify)
KOS_ELECTOR_ELECT(fast_item_const_store_fast,
                  kos_elector_fast_item_const_store_fast_classify)

#undef KOS_STACK_AT
#undef KOS_FAST_AT
#undef KOS_ITEM_CONST_CLASSIFY
#undef KOS_ITEM_CONST_STORE_BODIES
#undef KOS_ITEM_CONST_STORE_GENERIC
#undef KOS_ITEM_CONST_SEQ_BODY
#undef KOS_OBJ_STACK
#undef KOS_OBJ_FAST
#undef KOS_OBJ_FAST_OWNED
#undef KOS_VAL_STACK
#undef KOS_VAL_CONST
#undef KOS_VAL_FAST
#undef KOS_VAL_FAST_OWNED


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
#undef KOS_FAST_ITEM_CONST_SEQ_BODY
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
