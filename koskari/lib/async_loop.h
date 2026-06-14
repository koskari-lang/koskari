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


#ifndef KOSKARI_ASYNC_LOOP_H
#define KOSKARI_ASYNC_LOOP_H


#include <Python.h>


/**
   Run a coroutine without asyncio.Runner SIGINT handling

   :param coro: awaitable to run to completion
   :return: coroutine result with new reference, or NULL on error
 */

PyObject *kos_drive_async_loop(PyObject *coro);


int koskari_async_loop_init(PyObject *mod);
int koskari_async_loop_clear(PyObject *mod);


#endif


// The end.
