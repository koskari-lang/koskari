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

   See design/interpreter/electors.md.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


/* === Elector prototype and slot settlement === */


/* Every elector has the KosThreadFn prototype, as a handler does. It is
   a direct musttail target, so it stays out of line. insn is the site's
   own KosOp; value is the delivery argument, unused by the families
   that read their operands off the stack. */
#define KOS_ELECTOR(name)                                               \
  static KOS_NOINLINE KOS_PRESERVE_NONE KosOpResult el_##name(          \
      KosEvalState *es, void *insn, PyObject *value)


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
static inline void kos_elector_miss(
    KosCacheSlot *slot,
    KosThreadFn generic,
    uint8_t limit) {

  uint8_t n;

  if (! slot || slot->kind != KOS_CACHE_ELECTOR)
    return;

  KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_MISS);
  n = (uint8_t) (slot->u.elector.misses + 1);
  if (n >= limit) {
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_BYPASS);
    slot->u.elector.fn = generic;
    slot->u.elector.misses = 0;
    slot->u.elector.mode = KOS_ELECTOR_MEGAMORPHIC;
    return;
  }
  slot->u.elector.misses = n;
}


/* === OBJECT_OPERAND family === */


/* Guard misses since the election before an operand site demotes. */
#define KOS_ELECTOR_OPERAND_MISS_LIMIT 8


/* Under the safety net an int body refuses a site whose operand count
   is not the two it was elected for. */
#if KOS_EVAL_SAFETYNET
# define KOS_ELECTOR_OPERAND_SAFETYNET(insn)                            \
  do {                                                                  \
    if (unlikely(KosOpCount_COUNT(KosOp_ARG(insn)) != 2)) {             \
      PyErr_SetString(PyExc_SystemError,                                \
                      "int elector on an operand site of count != 2");  \
      return EVAL_RAISE();                                              \
    }                                                                   \
  } while (0)
#else
# define KOS_ELECTOR_OPERAND_SAFETYNET(insn)  ((void) 0)
#endif


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
  kos_elector_miss(KosOp_SLOT(insn), el_operand_generic,
                   KOS_ELECTOR_OPERAND_MISS_LIMIT);
  TAILCALL(el_operand_generic, es, insn, value);
}


/* Write the result over the left operand and drop the right one. Both
   operands are the stack's references, released only after the
   arithmetic that read them; the drop releases the right one. */
static inline void kos_operand_replace2(
    KosSegment **bl,
    PyObject *const *args,
    PyObject *out) {

  PyObject *left = args[0];

  ((PyObject **) args)[0] = out;
  kos_stack_drop1_fast(bl);
  Py_DECREF(left);
}


/**
   One int body per operator: the guard is the two exact-int checks on
   the peeked operands, then the operator's own fast path. A guard miss
   counts toward the demotion; an int the fast path declines (wider
   than a long long, an overflowing result, a negative shift count)
   takes the generic body for this execution without counting, since
   the site is still an int site.
*/
#define KOS_OPERAND_INT_ELECTOR(NAME, FAST)                             \
  KOS_ELECTOR(operand_int_##NAME) {                                     \
    KosSegment **bl = KosEvalState_BLOCK(es);                           \
    PyObject *const *args = kos_stack_tail_ptr_fast(*bl, 2);            \
    PyObject *out;                                                      \
    int rc;                                                             \
                                                                        \
    KOS_ELECTOR_OPERAND_SAFETYNET(insn);                                \
    if (unlikely(! args))                                               \
      RESULT_RAISE();                                                   \
    if (unlikely(! (PyLong_CheckExact(args[0])                          \
                    && PyLong_CheckExact(args[1]))))                    \
      TAILCALL(el_operand_miss, es, insn, value);                       \
                                                                        \
    rc = FAST(args[0], args[1], &out);                                  \
    if (unlikely(rc <= 0)) {                                            \
      if (rc < 0)                                                       \
        RESULT_RAISE();                                                 \
      TAILCALL(el_operand_generic, es, insn, value);                    \
    }                                                                   \
                                                                        \
    KOS_TRACE_OPCACHE_INC(KOS_OC_ELECTOR_HIT);                          \
    kos_operand_replace2(bl, args, out);                                \
    RESULT_NEXT(es);                                                    \
  }

KOS_OPERAND_INT_ELECTOR(add, long_fast_add)
KOS_OPERAND_INT_ELECTOR(subtract, long_fast_subtract)
KOS_OPERAND_INT_ELECTOR(multiply, long_fast_multiply)
KOS_OPERAND_INT_ELECTOR(modulo, long_fast_modulo)
KOS_OPERAND_INT_ELECTOR(bit_and, long_fast_bit_and)
KOS_OPERAND_INT_ELECTOR(bit_or, long_fast_bit_or)
KOS_OPERAND_INT_ELECTOR(bit_xor, long_fast_bit_xor)
KOS_OPERAND_INT_ELECTOR(bit_left, long_fast_bit_left)
KOS_OPERAND_INT_ELECTOR(bit_right, long_fast_bit_right)

#undef KOS_OPERAND_INT_ELECTOR


/* The int body per operator; NULL where the operator has no int fast
   path (divide, floor-divide, matrix multiply, power). */
static const KosThreadFn kos_operand_int_bodies[KOS_OPERAND_COUNT] = {
  [KOS_OPERAND_ADD] = el_operand_int_add,
  [KOS_OPERAND_SUBTRACT] = el_operand_int_subtract,
  [KOS_OPERAND_MULTIPLY] = el_operand_int_multiply,
  [KOS_OPERAND_MODULO] = el_operand_int_modulo,
  [KOS_OPERAND_BIT_AND] = el_operand_int_bit_and,
  [KOS_OPERAND_BIT_OR] = el_operand_int_bit_or,
  [KOS_OPERAND_BIT_XOR] = el_operand_int_bit_xor,
  [KOS_OPERAND_BIT_LEFT] = el_operand_int_bit_left,
  [KOS_OPERAND_BIT_RIGHT] = el_operand_int_bit_right,
};


/**
   The electing function, reached from an empty slot or an ungated
   site: classify the operands once and settle the slot. An exact-int
   pair at a binary site elects the operator's int body; anything else
   elects the generic body, which never re-elects.
*/
KOS_ELECTOR(operand_elect) {
  uint16_t arg = KosOp_ARG(insn);
  KosOperand op = (KosOperand) KosOpCount_TYPE(arg);
  KosSegment *s = *KosEvalState_BLOCK(es);
  KosThreadFn fn = NULL;

  if (KosOpCount_COUNT(arg) == 2 && op < KOS_OPERAND_COUNT
      && s && s->len >= 2) {
    PyObject *const *args = kos_stack_tail_ptr_fast(s, 2);
    if (PyLong_CheckExact(args[0]) && PyLong_CheckExact(args[1]))
      fn = kos_operand_int_bodies[op];
  }
  if (! fn)
    fn = el_operand_generic;

  kos_elector_elect(KosOp_SLOT(insn), fn);
  TAILCALL(fn, es, insn, value);
}


// The end.
