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


struct KosPyAsyncGen;  /* defined in iter.h */


extern PyTypeObject KosDemandType;


int koskari_demand_init(PyObject *mod);
int koskari_demand_clear(PyObject *mod);

/**
   callable, args, kwargs are borrowed. Caller retains ownership.
*/
PyObject *Koskari_DemandSync(PyObject *callable, PyObject *args, PyObject *kwargs);

/**
   Like Koskari_DemandSync but stores result in out_result and returns int.
   0 = success, -1 = error (exception set). KosCallable run_eval converts
   AWAIT/YIELD into an in-eval KoskariSyncViolation (KOS_EV_RAISE), so return
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


#endif /* KOSKARI_DEMAND_H */


// The end.
