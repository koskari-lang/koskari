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


#ifndef KOSKARI_SYNC_H
#define KOSKARI_SYNC_H


#include <Python.h>


int koskari_sync_init(PyObject *mod);
int koskari_sync_clear(PyObject *mod);

/**
   awaitable is borrowed. Caller retains ownership.
*/
PyObject *Koskari_RunSync(PyObject *awaitable);

/**
   async_iterable is borrowed. Caller retains ownership.
*/
PyObject *Koskari_IterSync(PyObject *async_iterable);

/**
   callable, args, kwargs are borrowed. Caller retains ownership.
*/
PyObject *Koskari_DemandSync(PyObject *callable, PyObject *args, PyObject *kwargs);

/**
   Like Koskari_DemandSync but stores result in out_result and returns int.
   0 = success, 1 = suspension (no exception; caller raises own), -1 = error
   (exception set).

   callable, args, and kwargs are borrowed. Caller retains ownership.
 */
int Koskari_DemandSyncEx(PyObject *callable, PyObject *args, PyObject *kwargs,
                         PyObject **out_result);


#endif


// The end.
