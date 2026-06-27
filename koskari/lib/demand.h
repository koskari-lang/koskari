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


#ifndef KOSKARI_DEMAND_H
#define KOSKARI_DEMAND_H


#include <Python.h>

#include "callable.h"


extern KosCallableTypeObject KosDemandCallableType;


#define KosDemand_CheckExact(obj) \
  ((obj) && ((obj)->ob_type == KOS_CALLABLE_PYTYPE(KosDemandCallableType)))


/**
   Return the inner callable held by a Demand instance.

   obj must be a Demand. Returns a borrowed reference.
*/
PyObject *KosDemand_GetWrapped(PyObject *obj);


/**
   Create a new Demand wrapping the given callable. Returns a new reference.
   Idempotent: if wrapped is already a Demand, the same object is returned.

   wrapped is borrowed. Caller retains ownership.
*/
PyObject *KosDemand_New(PyObject *wrapped);


int koskari_demand_init(PyObject *mod);
int koskari_demand_clear(PyObject *mod);

/**
   callable, args, kwargs are borrowed. Caller retains ownership.
*/
PyObject *Koskari_DemandSync(PyObject *callable, PyObject *args, PyObject *kwargs);

/**
   Like Koskari_DemandSync but stores result in out_result and returns int.
   0 = success, -1 = error (exception set). KosCallable run_eval converts
   AWAIT/YIELD into an in-eval KoskariSyncViolation (KOS_RES_RAISE), so return
   value 1 is not produced for that path; callers may still treat 1 as suspend
   for forward compatibility.

   callable, args, and kwargs are borrowed. Caller retains ownership.
 */
int Koskari_DemandSyncEx(PyObject *callable, PyObject *args, PyObject *kwargs,
                         PyObject **out_result);


/**
   Build a DemandGenerator for tp_iter: new KosContext, GEN installed from
   agen (kos_dyn_gen_install). Caller must not overlap with async iteration
*/
PyObject *KosDemandGenerator_FromAsyncGen(struct KosPyAsyncGen *agen);


/**
   Set KoskariSyncViolation on PyErr with msg as the message string.

   Called from eval guard sites (kos_eval_demand_deny_suspend) to raise the
   violation as an in-eval exception without duplicating exception registration.
*/
void kos_demand_set_sync_violation(const char *msg);


#endif /* KOSKARI_DEMAND_H */


// The end.
