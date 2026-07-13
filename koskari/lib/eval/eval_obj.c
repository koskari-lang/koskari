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


/* Dispatch the operator selector to the reducing operator function, applied to
   values[0..count-1] (values[0] is the accumulator seed). The fast PyLong path
   is folded into each KosOperator_* function, so this is a direct call. Returns
   a new reference or NULL. */
static PyObject *kos_operand_reduce(
    KosOperand op,
    PyObject *const *values,
    size_t count) {

  switch (op) {
    case KOS_OPERAND_ADD:             return KosOperator_Add(values, count);
    case KOS_OPERAND_SUBTRACT:        return KosOperator_Subtract(values, count);
    case KOS_OPERAND_MULTIPLY:        return KosOperator_Multiply(values, count);
    case KOS_OPERAND_MODULO:          return KosOperator_Modulo(values, count);
    case KOS_OPERAND_BIT_AND:         return KosOperator_BitwiseAnd(values, count);
    case KOS_OPERAND_BIT_OR:          return KosOperator_BitwiseOr(values, count);
    case KOS_OPERAND_BIT_XOR:         return KosOperator_BitwiseXor(values, count);
    case KOS_OPERAND_BIT_LEFT:        return KosOperator_Lshift(values, count);
    case KOS_OPERAND_BIT_RIGHT:       return KosOperator_Rshift(values, count);
    case KOS_OPERAND_TRUE_DIVIDE:     return KosOperator_TrueDivide(values, count);
    case KOS_OPERAND_FLOOR_DIVIDE:    return KosOperator_FloorDivide(values, count);
    case KOS_OPERAND_MATRIX_MULTIPLY: return KosOperator_MatrixMultiply(values, count);
    case KOS_OPERAND_POWER:           return KosOperator_Power(values, count);
    default:
      PyErr_Format(PyExc_SystemError, "bad operand op-type: %i", (int) op);
      return NULL;
  }
}


/* In-place counterpart of kos_operand_reduce: dispatch to the KosOperator_-
   Inplace* reducing function over values[0..count-1] (values[0] is the
   accumulator seed). Fast PyLong path folded into each function. Returns a new
   reference or NULL. */
static PyObject *kos_operand_inplace(
    KosOperand op,
    PyObject *const *values,
    size_t count) {

  switch (op) {
    case KOS_OPERAND_ADD:             return KosOperator_InplaceAdd(values, count);
    case KOS_OPERAND_SUBTRACT:        return KosOperator_InplaceSubtract(values, count);
    case KOS_OPERAND_MULTIPLY:        return KosOperator_InplaceMultiply(values, count);
    case KOS_OPERAND_MODULO:          return KosOperator_InplaceRemainder(values, count);
    case KOS_OPERAND_BIT_AND:         return KosOperator_InplaceBitwiseAnd(values, count);
    case KOS_OPERAND_BIT_OR:          return KosOperator_InplaceBitwiseOr(values, count);
    case KOS_OPERAND_BIT_XOR:         return KosOperator_InplaceBitwiseXor(values, count);
    case KOS_OPERAND_BIT_LEFT:        return KosOperator_InplaceLshift(values, count);
    case KOS_OPERAND_BIT_RIGHT:       return KosOperator_InplaceRshift(values, count);
    case KOS_OPERAND_TRUE_DIVIDE:     return KosOperator_InplaceTrueDivide(values, count);
    case KOS_OPERAND_FLOOR_DIVIDE:    return KosOperator_InplaceFloorDivide(values, count);
    case KOS_OPERAND_MATRIX_MULTIPLY: return KosOperator_InplaceMatrixMultiply(values, count);
    case KOS_OPERAND_POWER:           return KosOperator_InplacePower(values, count);
    default:
      PyErr_Format(PyExc_SystemError, "bad inplace operand op-type: %i", (int) op);
      return NULL;
  }
}


/* Reduce `count` stack values through op and push the result. Peeks the count
   values (obj_operand: seed is values[0]), applies the reducing operator, drops
   them, pushes. Forced noinline: the switch stays out of the dispatch loop. */
static KOS_NOINLINE int kos_object_operand(
    KosEvalState *es,
    KosOperand op,
    int count) {

  PyObject *const *args = NULL;
  if (count > 0) {
    args = KosEvalState_StackTailPtr(es, (size_t) count);
    if (! args)
      return -1;
  }

  PyObject *res = kos_operand_reduce(op, args, (size_t) count);

  if (count > 0)
    KosEvalState_StackDropNoErr(es, (size_t) count);
  if (! res)
    return -1;
  return KosEvalState_StackPUSH(es, res);
}


/* Stack-based OBJECT_INPLACE: reduce `count` stack values through the in-place
   operator and push the result -- the accumulator form used by inplace-item. */
static KOS_NOINLINE int kos_object_inplace(
    KosEvalState *es,
    KosOperand op,
    int count) {

  PyObject *const *args = NULL;
  if (count > 0) {
    args = KosEvalState_StackTailPtr(es, (size_t) count);
    if (! args)
      return -1;
  }

  PyObject *res = kos_operand_inplace(op, args, (size_t) count);

  if (count > 0)
    KosEvalState_StackDropNoErr(es, (size_t) count);
  if (! res)
    return -1;
  return KosEvalState_StackPUSH(es, res);
}


/* === In-place slot and name cores === */


/* Augmented assignment to a fast-slot binding: obj = deref(slot); obj OP= rhs;
   store back. Binary (the accumulator is the slot value, one rhs off the
   stack). Routes through the in-place reducing operator. Returns 0/-1. */
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

  PyObject *pair[2] = { obj, rhs };
  PyObject *result = kos_operand_inplace(op, pair, 2);

  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return -1;

  int res = kos_ns_fast_store(ns, idx, result);
  Py_DECREF(result);
  return res;
}


/* Augmented assignment to a frame-namespace binding: mirror of
   kos_fast_inplace_operand for names resolved through the frame namespace.
   Returns 0/-1. */
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

  PyObject *pair[2] = { obj, rhs };
  PyObject *result = kos_operand_inplace(op, pair, 2);

  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return -1;

  int res = frame_ns_store(es, name, result);
  Py_DECREF(result);
  return res;
}


/* Shared core for the augmented attribute-assignment family (ATTR_INPLACE and
   the FAST / VAR slot variants).  Reduces obj.attr in place:

     acc = getattr(obj, attr)
     for each of the count values in args: acc = acc OP= args[i]
     setattr(obj, attr, acc)

   Each fold step routes through the in-place reducing operator (a two-operand
   in-place reduce), so left-to-right over count values matches a single
   reduction seeded with acc.  args points at the count reduction values
   (typically a stack tail); this function neither pops them nor touches obj's
   ref -- the caller owns both and drops the stack afterwards.  Returns 0/-1. */
static int kos_attr_inplace_reduce(
    PyObject *obj,
    PyObject *attr,
    KosOperand op,
    PyObject *const *args,
    int count) {

  PyObject *acc = PyObject_GetAttr(obj, attr);
  if (! acc)
    return -1;

  for (int i = 0; i < count; i++) {
    PyObject *pair[2] = { acc, args[i] };
    PyObject *n = kos_operand_inplace(op, pair, 2);
    Py_DECREF(acc);
    acc = n;
    if (! acc)
      return -1;
  }

  int res = PyObject_SetAttr(obj, attr, acc);
  Py_DECREF(acc);
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
