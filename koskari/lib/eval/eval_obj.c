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
  koskari/lib/eval/eval_obj.c

  Object-operator opcode helpers, gathered out of eval.c: the parameterized
  arithmetic operand family (OBJECT_OPERAND), the in-place families
  (OBJECT_INPLACE and the FAST / VAR slot variants), and the comparison /
  predicate result helpers. Included into eval.c as a segment (see the eval/
  includes there); relies on eval.c's includes and is pulled in ahead of the
  opcode handlers that call these.

  :author: Christopher O'Brien <obriencj@preoccupied.net>
  :license: GNU General Public License v3
*/


/* Defined in eval.c: the frame-namespace deref/store helpers, shared with the
   VAR / CALL_VAR / IMPORT opcode families. Forward-declared here because this
   segment is included ahead of their definitions. */
static PyObject *frame_ns_deref(KosEvalState *es, PyObject *name);
static int frame_ns_store(KosEvalState *es, PyObject *name, PyObject *val);


/* === Operand cores; OBJECT_OPERAND / OBJECT_INPLACE dispatch === */


/**
   Reducing operator core: pop `count` values (via StackTailPtr for count > 0),
   call fn, drop them, push the result.  count is supplied by the caller (the
   OPCOUNT low byte for OBJECT_OPERAND, or KosOp_ARG for the insn wrapper).
   Faithful to ceval _obj_op.
 */
static int obj_op_reduce_n(
    KosEvalState *es,
    int count,
    KosOperatorReducing *fn) {

  PyObject *res;

  if (count < 0) {
    PyErr_Format(PyExc_RuntimeError,
                 "Negative count argument to reducing operator: %i", count);
    return -1;
  }

  if (count == 0) {
    res = fn(NULL, 0);
  } else {
    PyObject *const *args = KosEvalState_StackTailPtr(es, (size_t) count);
    if (! args)
      return -1;
    res = fn(args, (size_t) count);
    KosEvalState_StackDropNoErr(es, (size_t) count);
  }

  if (! res)
    return -1;
  return KosEvalState_StackPUSH(es, res);
}


/* insn-based wrapper: count from KosOp_ARG.  Still used by the COUNT-style
   reducing handlers that were not consolidated into OBJECT_OPERAND (e.g.
   OBJECT_STR / KosOperator_BuildStr). */
static int obj_op_reducing(
    KosEvalState *es,
    KosOp *insn,
    KosOperatorReducing *fn) {

  return obj_op_reduce_n(es, (int) KosOp_ARG(insn), fn);
}


/**
   Reducing operator with a two-operand PyLong fast path.  When count == 2 and
   the top two stack values qualify for kos_long_binop_fast, compute directly
   and skip the generic reduce dispatch; any other count, type, or overflow
   falls through to obj_op_reduce_n with fn.  lop may be KOS_LBOP_NONE for
   operators with no int-fast path (the fast check then always declines).
*/
static int obj_op_reduce_n_long(
    KosEvalState *es,
    int count,
    KosOperatorReducing *fn,
    KosLongBinop lop) {

  if (likely(count == 2)) {
    PyObject *const *args = KosEvalState_StackTailPtr(es, 2);
    if (unlikely(! args))
      return -1;

    PyObject *res = NULL;
    int handled = kos_long_binop_fast(lop, args[0], args[1], &res);
    if (unlikely(handled < 0))
      return -1;
    if (likely(handled)) {
      KosEvalState_StackDropNoErr(es, 2);
      return KosEvalState_StackPUSH(es, res);
    }
  }

  return obj_op_reduce_n(es, count, fn);
}


static int obj_op_binary(
    KosEvalState *es,
    KosOp *insn,
    PyObject *(*fn)(PyObject *left, PyObject *right)) {

  KosEvalStackLR op = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, op))
    return -1;
  PyObject *res = fn(op.left, op.right);
  Py_DECREF(op.left);
  Py_DECREF(op.right);
  if (! res)
    return -1;
  (void) insn;
  return KosEvalState_StackPUSH(es, res);
}


/**
   Binary operator with a PyLong fast path.  Pops exactly two values;
   qualifying int operands skip fn entirely.  lop may be KOS_LBOP_NONE.
*/
static int obj_op_binary_n(
    KosEvalState *es,
    PyObject *(*fn)(PyObject *left, PyObject *right),
    KosLongBinop lop) {

  KosEvalStackLR op = {0};
  if (KOS_EVALSTATE_POP_ARGS(es, op))
    return -1;

  PyObject *res = NULL;
  int handled = kos_long_binop_fast(lop, op.left, op.right, &res);
  if (handled == 0)
    res = fn(op.left, op.right);
  Py_DECREF(op.left);
  Py_DECREF(op.right);
  if (! res)
    return -1;
  return KosEvalState_StackPUSH(es, res);
}


/**
   Power operator: count == 2 -> pow(base, exp); count == 3 -> pow(base, exp,
   mod).  Any other count is an error.  Pops the operands newest-first (mod,
   exp, base), pushes the result.
*/
static int obj_op_power_n(KosEvalState *es, int count) {

  if (count < 2 || count > 3) {
    PyErr_Format(PyExc_RuntimeError,
                 "power requires 2 or 3 arguments, got %i", count);
    return -1;
  }

  PyObject *mod = Py_None;
  if (count == 3) {
    mod = KosEvalState_StackPopRef(es);
    if (! mod)
      return -1;
  }

  PyObject *exp = KosEvalState_StackPopRef(es);
  if (! exp) {
    if (count == 3)
      Py_DECREF(mod);
    return -1;
  }

  PyObject *base = KosEvalState_StackPopRef(es);
  if (! base) {
    Py_DECREF(exp);
    if (count == 3)
      Py_DECREF(mod);
    return -1;
  }

  PyObject *val = PyNumber_Power(base, exp, mod);
  Py_DECREF(base);
  Py_DECREF(exp);
  if (count == 3)
    Py_DECREF(mod);
  if (! val)
    return -1;
  return KosEvalState_StackPUSH(es, val);
}


/**
   Shared out-of-line body for the OBJECT_OPERAND family.  Dispatches the
   op-type selector (KosOperand) to the operator's exact semantics: the nine
   truly-reducing operators fold `count` operands (six with an int-fast path);
   modulo and the two bit-shifts are strictly binary; power is 2-or-3-arg.
   Each case passes a *constant* KosOperator fn and KOS_LBOP_*, so the int-fast
   path stays specialized per operator.  Forced noinline: the switch is a
   genuine runtime branch, kept out of the inlined dispatch loop.  Returns
   0/-1 for kos_op_result_from_push_int. */
static KOS_NOINLINE int kos_object_operand(
    KosEvalState *es,
    KosOperand op,
    int count) {

  switch (op) {
    case KOS_OPERAND_ADD:
      return obj_op_reduce_n_long(es, count, KosOperator_Add, KOS_LBOP_ADD);
    case KOS_OPERAND_SUBTRACT:
      return obj_op_reduce_n_long(es, count, KosOperator_Subtract,
                                  KOS_LBOP_SUBTRACT);
    case KOS_OPERAND_MULTIPLY:
      return obj_op_reduce_n_long(es, count, KosOperator_Multiply,
                                  KOS_LBOP_MULTIPLY);
    case KOS_OPERAND_BIT_AND:
      return obj_op_reduce_n_long(es, count, KosOperator_BitwiseAnd,
                                  KOS_LBOP_BIT_AND);
    case KOS_OPERAND_BIT_OR:
      return obj_op_reduce_n_long(es, count, KosOperator_BitwiseOr,
                                  KOS_LBOP_BIT_OR);
    case KOS_OPERAND_BIT_XOR:
      return obj_op_reduce_n_long(es, count, KosOperator_BitwiseXor,
                                  KOS_LBOP_BIT_XOR);
    case KOS_OPERAND_FLOOR_DIVIDE:
      return obj_op_reduce_n(es, count, KosOperator_FloorDivide);
    case KOS_OPERAND_MATRIX_MULTIPLY:
      return obj_op_reduce_n(es, count, KosOperator_MatrixMultiply);
    case KOS_OPERAND_TRUE_DIVIDE:
      return obj_op_reduce_n(es, count, KosOperator_TrueDivide);
    case KOS_OPERAND_MODULO:
      return obj_op_binary_n(es, PyNumber_Remainder, KOS_LBOP_MODULO);
    case KOS_OPERAND_BIT_LEFT:
      return obj_op_binary_n(es, PyNumber_Lshift, KOS_LBOP_BIT_LEFT);
    case KOS_OPERAND_BIT_RIGHT:
      return obj_op_binary_n(es, PyNumber_Rshift, KOS_LBOP_BIT_RIGHT);
    case KOS_OPERAND_POWER:
      return obj_op_power_n(es, count);
    default:
      PyErr_Format(PyExc_SystemError,
                   "bad operand op-type: %i", (int) op);
      return -1;
  }
}


/**
   Shared out-of-line body for the stack-based OBJECT_INPLACE family.  Reduces
   `count` operands from the stack left-to-right through the operator's
   PyNumber_InPlace* reducer (KosOperator_Inplace*) and pushes the result --
   the accumulator form used by the inplace-item / inplace-attr special forms.
   Modulo keeps the two-operand int-fast path; power is strictly binary
   (KosOperator_InplacePower rejects count != 2).  Each case passes a constant
   fn.  Forced noinline: the switch stays out of the inlined dispatch loop.
   Returns 0/-1 for kos_op_result_from_push_int. */
static KOS_NOINLINE int kos_object_inplace(
    KosEvalState *es,
    KosOperand op,
    int count) {

  switch (op) {
    case KOS_OPERAND_ADD:
      return obj_op_reduce_n(es, count, KosOperator_InplaceAdd);
    case KOS_OPERAND_SUBTRACT:
      return obj_op_reduce_n(es, count, KosOperator_InplaceSubtract);
    case KOS_OPERAND_MULTIPLY:
      return obj_op_reduce_n(es, count, KosOperator_InplaceMultiply);
    case KOS_OPERAND_MODULO:
      return obj_op_reduce_n_long(es, count, KosOperator_InplaceRemainder,
                                  KOS_LBOP_MODULO);
    case KOS_OPERAND_BIT_AND:
      return obj_op_reduce_n(es, count, KosOperator_InplaceBitwiseAnd);
    case KOS_OPERAND_BIT_OR:
      return obj_op_reduce_n(es, count, KosOperator_InplaceBitwiseOr);
    case KOS_OPERAND_BIT_XOR:
      return obj_op_reduce_n(es, count, KosOperator_InplaceBitwiseXor);
    case KOS_OPERAND_BIT_LEFT:
      return obj_op_reduce_n(es, count, KosOperator_InplaceLshift);
    case KOS_OPERAND_BIT_RIGHT:
      return obj_op_reduce_n(es, count, KosOperator_InplaceRshift);
    case KOS_OPERAND_TRUE_DIVIDE:
      return obj_op_reduce_n(es, count, KosOperator_InplaceTrueDivide);
    case KOS_OPERAND_FLOOR_DIVIDE:
      return obj_op_reduce_n(es, count, KosOperator_InplaceFloorDivide);
    case KOS_OPERAND_MATRIX_MULTIPLY:
      return obj_op_reduce_n(es, count, KosOperator_InplaceMatrixMultiply);
    case KOS_OPERAND_POWER:
      return obj_op_reduce_n(es, count, KosOperator_InplacePower);
    default:
      PyErr_Format(PyExc_SystemError,
                   "bad inplace operand op-type: %i", (int) op);
      return -1;
  }
}


/* === In-place slot and name cores === */


/* Shared out-of-line body for the fast-slot in-place family.  Forced
   noinline so the op-type switch is a genuine runtime (polymorphic) branch,
   matching the eventual FAST_OBJECT_INPLACE handler reading op from its
   operand.  Binary (count 1) for the gate; reducing generalization is later.
   Returns 0/-1 for kos_op_result_from_push_int.  Operator set is
   KOS_OPERAND_LIST (operands.h). */
static KOS_NOINLINE int kos_fast_inplace_operand(
    KosEvalState *es, KosOperand op, size_t idx) {

  PyObject *ns = KosEvalState_NAMESPACE(es);
  PyObject *obj = kos_ns_fast_deref(ns, idx);
  if (unlikely(! obj))
    return -1;

  PyObject *rhs = KosEvalState_StackPopRef(es);
  if (unlikely(! rhs)) {
    Py_DECREF(obj);
    return -1;
  }

  PyObject *result = NULL;
  switch (op) {
#define KOS_OPERAND_INPLACE_CASE(NAME, LOP, RFN, IFN)                    \
    case KOS_OPERAND_##NAME:                                            \
      if (kos_long_binop_fast(LOP, obj, rhs, &result) == 0)          \
        result = IFN(obj, rhs);                                       \
      break;
    KOS_OPERAND_LIST(KOS_OPERAND_INPLACE_CASE)
#undef KOS_OPERAND_INPLACE_CASE
    default:
      PyErr_SetString(PyExc_SystemError, "bad operand op-type");
  }

  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return -1;

  int res = kos_ns_fast_store(ns, idx, result);
  Py_DECREF(result);
  return res;
}


/* Shared out-of-line body for the var-name in-place family (mirror of
   kos_fast_inplace_operand for names resolved through the frame namespace).
   Forced noinline so the op-type switch stays a real runtime branch out of
   the dispatch loop.  Binary (count 1) for now.  Returns 0/-1. */
static KOS_NOINLINE int kos_var_inplace_operand(
    KosEvalState *es, KosOperand op, PyObject *name) {

  PyObject *obj = frame_ns_deref(es, name);
  if (unlikely(! obj))
    return -1;

  PyObject *rhs = KosEvalState_StackPopRef(es);
  if (unlikely(! rhs)) {
    Py_DECREF(obj);
    return -1;
  }

  PyObject *result = NULL;
  switch (op) {
#define KOS_OPERAND_INPLACE_CASE(NAME, LOP, RFN, IFN)                    \
    case KOS_OPERAND_##NAME:                                            \
      if (kos_long_binop_fast(LOP, obj, rhs, &result) == 0)          \
        result = IFN(obj, rhs);                                       \
      break;
    KOS_OPERAND_LIST(KOS_OPERAND_INPLACE_CASE)
#undef KOS_OPERAND_INPLACE_CASE
    default:
      PyErr_SetString(PyExc_SystemError, "bad operand op-type");
  }

  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return -1;

  int res = frame_ns_store(es, name, result);
  Py_DECREF(result);
  return res;
}


/* === Comparison and predicate result helpers === */


static PyObject *binary_in_result(PyObject *left, PyObject *right) {
  int r = PySequence_Contains(right, left);
  if (r < 0)
    return NULL;
  return PyBool_FromLong(r);
}


static PyObject *binary_not_in_result(PyObject *left, PyObject *right) {
  int r = PySequence_Contains(right, left);
  if (r < 0)
    return NULL;
  return PyBool_FromLong(! r);
}


static PyObject *binary_is_result(PyObject *left, PyObject *right) {
  return PyBool_FromLong(left == right);
}


static PyObject *binary_is_not_result(PyObject *left, PyObject *right) {
  return PyBool_FromLong(left != right);
}


// The end.
