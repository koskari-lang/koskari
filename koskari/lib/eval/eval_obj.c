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


/* The operator-selector dispatchers kos_operand_reduce / kos_inplace_reduce
   live in operator.c (declared in operator.h) alongside the per-operator cores
   they switch into; the handlers below dispatch through them. */


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

  PyObject *res = kos_inplace_reduce(op, args, (size_t) count);

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
  PyObject *result = kos_inplace_reduce(op, pair, 2);

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
  PyObject *result = kos_inplace_reduce(op, pair, 2);

  Py_DECREF(obj);
  Py_DECREF(rhs);
  if (unlikely(! result))
    return -1;

  int res = frame_ns_store(es, name, result);
  Py_DECREF(result);
  return res;
}


/* Fold acc in place over the count reduction values: acc = acc OP= args[i],
   left to right.  Consumes the (owned) acc reference and returns the owned
   result, or NULL on error (acc already dropped).  Each step routes through the
   in-place reducing operator, so this matches a single reduction seeded with
   acc.  Shared by the getattr/setattr generic path and the field-cache fast
   path (eval_cache.c), which differ only in how acc is loaded and stored. */
static PyObject *attr_inplace_fold(
    PyObject *acc,
    KosOperand op,
    PyObject *const *args,
    int count) {

  for (int i = 0; i < count; i++) {
    PyObject *pair[2] = { acc, args[i] };
    PyObject *n = kos_inplace_reduce(op, pair, 2);
    Py_DECREF(acc);
    acc = n;
    if (! acc)
      return NULL;
  }

  return acc;
}


/* Shared core for the augmented attribute-assignment family (ATTR_INPLACE and
   the FAST / VAR slot variants).  Reduces obj.attr in place:

     acc = getattr(obj, attr)
     acc = fold(acc) over the count values in args
     setattr(obj, attr, acc)

   args points at the count reduction values (typically a stack tail); this
   function neither pops them nor touches obj's ref -- the caller owns both and
   drops the stack afterwards.  Returns 0/-1. */
static int kos_attr_inplace_reduce(
    PyObject *obj,
    PyObject *attr,
    KosOperand op,
    PyObject *const *args,
    int count) {

  PyObject *acc = PyObject_GetAttr(obj, attr);
  if (! acc)
    return -1;

  acc = attr_inplace_fold(acc, op, args, count);
  if (! acc)
    return -1;

  int res = PyObject_SetAttr(obj, attr, acc);
  Py_DECREF(acc);
  return res;
}


// The end.
