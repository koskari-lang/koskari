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


#ifndef KOSKARI_FORMALS_H
#define KOSKARI_FORMALS_H


#include <Python.h>
#include "namespace.h"
#include "stack.h"


/* === FormalsSpec === */


extern PyTypeObject KosFormalsSpecType;


typedef struct KosFormalsSpec {
  PyObject_HEAD

  PyObject *pos;
  PyObject *pos_only;
  PyObject *kwd;
  PyObject *kwd_only;
  PyObject *star;
  PyObject *starstar;
  PyObject *by_refs;

  PyObject *formals;  /* the KosFormals over this spec with no defaults,
                         built at construction. A Formals is never
                         written after it is built, so every function
                         made from this spec without defaults shares it
                         (KosFormals_New) */

} KosFormalsSpec;


PyObject *KosFormalsSpec_New(
    PyObject *pos,
    PyObject *pos_only,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *star,
    PyObject *starstar,
    PyObject *by_refs);


PyObject *KosFormalsSpec_NamespaceSpec(
    PyObject *fspec,
    PyObject *base_nspec);


#define KosFormalsSpec_POS(fmls)                \
  (((KosFormalsSpec *) fmls)->pos)

#define KosFormalsSpec_POS_ONLY(fmls)           \
  (((KosFormalsSpec *) fmls)->pos_only)

#define KosFormalsSpec_KWD(fmls)                \
  (((KosFormalsSpec *) fmls)->kwd)

#define KosFormalsSpec_KWD_ONLY(fmls)           \
  (((KosFormalsSpec *) fmls)->kwd_only)

#define KosFormalsSpec_STAR(fmls)               \
  (((KosFormalsSpec *) fmls)->star)

#define KosFormalsSpec_STARSTAR(fmls)           \
  (((KosFormalsSpec *) fmls)->starstar)

#define KosFormalsSpec_BY_REFS(fmls)            \
  (((KosFormalsSpec *) fmls)->by_refs)

#define KosFormalsSpec_FORMALS(fmls)            \
  (((KosFormalsSpec *) fmls)->formals)

#define KosFormalsSpec_CheckExact(obj)                  \
  ((obj) && ((obj)->ob_type == &KosFormalsSpecType))


/* === Formals === */


extern PyTypeObject KosFormalsType;


typedef struct KosFormals {
  PyObject_HEAD

  PyObject *pos;
  PyObject *pos_only;
  PyObject *kwd;
  PyObject *kwd_only;
  PyObject *star;
  PyObject *starstar;
  PyObject *defaults;
  PyObject *by_refs;

} KosFormals;


/**
   The Formals of spec with the given defaults. With defaults NULL the
   result is the spec's own shared Formals; pass NULL, not None, for no
   defaults. With a dict the result is a new Formals holding a copy.

   :return: a new reference, or NULL with PyErr set
*/
PyObject *KosFormals_New(PyObject *spec, PyObject *defaults);


PyObject *KosFormals_NewFull(
    PyObject *pos,
    PyObject *pos_only,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *star,
    PyObject *starstar,
    PyObject *defaults,
    PyObject *by_refs);


int KosFormals_Apply(
    PyObject *self,
    PyObject *namespace,
    PyObject *args,
    PyObject *kwds);


int KosFormals_ApplyFast(
    PyObject *self,
    PyObject *namespace,
    PyObject *args,
    PyObject *kwds);


int KosFormals_VectorApplyFast(
    PyObject *self,
    PyObject *namespace,
    PyObject *const *args,
    size_t nargs,
    PyObject *names);


/**
   Build a NamespaceSpec that includes the parameter names from formals.

   Same role as KosFormalsSpec_NamespaceSpec but for a KosFormals instance.
   Used when assembling a Function from (code, formals, parent_ns).

   :param formals: KosFormals instance
   :param base_nspec: optional base NamespaceSpec (or None)
   :return: new NamespaceSpec, or NULL on error
*/
PyObject *KosFormals_NamespaceSpec(
    PyObject *formals,
    PyObject *base_nspec);


#define KosFormals_POS(fmls)                \
  (((KosFormals *) fmls)->pos)

#define KosFormals_POS_ONLY(fmls)           \
  (((KosFormals *) fmls)->pos_only)

#define KosFormals_KWD(fmls)                \
  (((KosFormals *) fmls)->kwd)

#define KosFormals_KWD_ONLY(fmls)           \
  (((KosFormals *) fmls)->kwd_only)

#define KosFormals_STAR(fmls)               \
  (((KosFormals *) fmls)->star)

#define KosFormals_STARSTAR(fmls)           \
  (((KosFormals *) fmls)->starstar)

#define KosFormals_DEFAULTS(fmls)           \
  (((KosFormals *) fmls)->defaults)

#define KosFormals_BY_REFS(fmls)            \
  (((KosFormals *) fmls)->by_refs)

#define KosFormals_Check(obj)                          \
  ((obj) && ((obj)->ob_type == &KosFormalsType))


/* === Simple-call steal binding === */


/**
   Steal-bind positionals for the simple-call class: move args[0..nargs-1]
   into namespace fast slots 0..nargs-1.  Requires the simple-call
   classification (KosFunction simple_arity) and a namespace fresh from
   KosNamespace_Copy, so every target slot is raw positional and NULL --
   the bind is a bare pointer transfer with no refcount traffic.

   Ownership: the caller forfeits every reference in args; after this
   returns the namespace owns them all.  A buffer holding any borrowed
   entry (e.g. the self-arg prepend buffer, whose slot 0 is borrowed)
   must never be passed here.
*/
static inline int KosFormals_SimpleStealApply(
    PyObject *ns,
    PyObject *const *args,
    Py_ssize_t nargs) {

  /* one to three pointers on nearly every call: the plain loop, not
     the vector expansion (KOS_LOOP_SCALAR in hints.h) */
  KOS_LOOP_SCALAR
  for (Py_ssize_t i = 0; i < nargs; i++)
    KosNamespace_GET_FAST_VAL(ns, i) = args[i];
  return 0;
}


/**
   Steal-bind positionals from the tail of a segment: move the top
   nargs references into namespace fast slots 0..nargs-1 and retract the
   block length in the same step, so the transfer and the window
   accounting cannot come apart -- the moved span must not be dropped by
   the caller.  Same classification contract as
   KosFormals_SimpleStealApply.

   :return: 0 on success; -1 on underrun (error set, nothing moved)
*/
/**
   The underrun report of the steal helpers below, out of line so their
   bodies fold into the call path. Always returns -1 with the error set.
*/
int kos_formals_steal_underrun(const KosSegment *block, Py_ssize_t nargs);


KOS_OP_INLINE int KosFormals_SimpleStealApplyBlock(
    PyObject *ns,
    KosSegment *block,
    Py_ssize_t nargs) {

  if (! nargs)
    return 0;

  if (unlikely(! block || (size_t) nargs > KosSegment_LEN(block)))
    return kos_formals_steal_underrun(block, nargs);

  {
    size_t base = KosSegment_LEN(block) - (size_t) nargs;
    (void) KosFormals_SimpleStealApply(ns, &block->objs[base], nargs);
    KosSegment_LEN(block) = base;
  }
  return 0;
}


/**
   Steal-bind positionals with an out-of-band receiver: slot 0 binds from
   self (a reference is taken here when self_owned is 0; when it is 1
   the caller's reference moves into the slot), and the top nargs block
   references move into slots 1..nargs with the block length retracted in
   the same step.  self never lives on the block, so the caller's window
   accounting is untouched by the injection.  Same classification
   contract as KosFormals_SimpleStealApply against a simple arity of
   nargs + 1, the receiver counted.

   :return: 0 on success; -1 on underrun (error set, nothing moved)
*/
KOS_OP_INLINE int KosFormals_SimpleStealApplySelfBlock(
    PyObject *ns,
    PyObject *self,
    int self_owned,
    KosSegment *block,
    Py_ssize_t nargs) {

  if (unlikely(nargs
               && (! block || (size_t) nargs > KosSegment_LEN(block))))
    return kos_formals_steal_underrun(block, nargs);

  if (! self_owned)
    Py_INCREF(self);
  KosNamespace_GET_FAST_VAL(ns, 0) = self;

  if (nargs) {
    size_t base = KosSegment_LEN(block) - (size_t) nargs;
    PyObject *const *args = &block->objs[base];

    /* the plain loop, as in KosFormals_SimpleStealApply */
    KOS_LOOP_SCALAR
    for (Py_ssize_t i = 0; i < nargs; i++)
      KosNamespace_GET_FAST_VAL(ns, (size_t) i + 1) = args[i];
    KosSegment_LEN(block) = base;
  }
  return 0;
}


/* === Unit lifecycle === */


int koskari_formals_init(PyObject *mod);
int koskari_formals_clear(const PyObject *mod);


#endif


// The end.
