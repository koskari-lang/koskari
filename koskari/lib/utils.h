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


#ifndef KOSKARI_UTILS_H
#define KOSKARI_UTILS_H


#include <Python.h>


/**
   Utility functions for Koskari runtime

   These are general-purpose helper functions exposed as module-level
   callables in koskari.lib. They provide common functional programming
   primitives and collection builders.
*/


/* === function application utilities === */


/**
   Apply a function repeatedly.

   Calls data = func(data) count times, returning the final result.
   If func raises an exception, returns NULL immediately.

   :param func: callable to apply
   :param data: initial data value
   :param count: number of times to apply
   :return: final result after count applications
*/
PyObject *KosUtils_Reapply(PyObject *func, PyObject *data, long count);


/**
   Apply a function to arguments.

   Calls func(*args, **kwargs), handling pair unpacking for args
   if the argument is a pair type.

   :param func: callable to invoke
   :param args: positional arguments (may be pair, tuple, or list)
   :param kwargs: keyword arguments dict (may be NULL)
   :return: result of function call
*/
PyObject *KosUtils_Apply(PyObject *func, PyObject *args, PyObject *kwargs);


/* === functional primitives === */


/**
   Identity function - returns its argument unchanged.

   :param value: any Python object
   :return: the same object (with incremented refcount)
*/
PyObject *KosUtils_Identity(PyObject *value);


/**
   Create a function that always returns the given value.

   :param value: the value to return
   :return: a callable that returns value when called
*/
PyObject *KosUtils_Constantly(PyObject *value);


/**
   Koskari-style string for an object.

   Dispatch order: plain ``str`` uses double-quote wrapping; then the
   type registry (``tp_base`` walk); then a callable ``__kos_repr__`` on
   the demand path; otherwise ``repr(obj)``. A user ``__kos_repr__`` or
   registry handler must return ``str`` or ``TypeError`` is raised.

   :param obj: any Python object
   :return: new unicode string, or NULL with an exception set
*/
PyObject *KosUtils_Repr(PyObject *obj);


/**
   Register a ``type`` to ``handler`` for KosUtils_Repr.

   ``type`` must be a type object. ``handler`` must be callable and must
   not itself be a type object. The handler is called with one argument,
   the value being printed, and must return ``str``.

   :param type: the type key
   :param handler: callable taking the object, returning str
   :return: 0 on success, -1 with an exception set on failure
*/
int KosUtils_ReprRegister(PyObject *type, PyObject *handler);


/**
   Register a builtin type for KosUtils_Repr using its ``__kos_repr__``.

   ``type`` must be a type object with a callable ``__kos_repr__`` (usually
   from ``tp_methods``). That attribute is looked up on the type and stored
   in the kos repr registry.

   :param type: the type key
   :return: 0 on success, -1 with an exception set on failure
*/
int KosUtils_ReprRegisterBuiltin(PyObject *type);


/**
   Remove ``type`` from the kos repr registry if present.

   :param type: the type key
   :return: 0 on success, -1 with an exception set on failure
*/
int KosUtils_ReprUnregister(PyObject *type);


/**
   Clear all entries from the kos repr type registry.
*/
void KosUtils_ReprRegistryClear(void);


/**
   Return a shallow copy of the kos repr type registry.

   :return: new dict, or NULL with an exception set on failure
*/
PyObject *KosUtils_ReprRegistrySnapshot(void);


/* === collection builders === */


/**
   Build a tuple from variadic arguments.

   :param args: arguments to pack into tuple
   :param nargs: number of arguments
   :return: new tuple containing args
*/
PyObject *KosUtils_BuildTuple(PyObject *const *args, Py_ssize_t nargs);


/**
   Build a list from variadic arguments.

   :param args: arguments to pack into list
   :param nargs: number of arguments
   :return: new list containing args
*/
PyObject *KosUtils_BuildList(PyObject *const *args, Py_ssize_t nargs);


/**
   Build a set from variadic arguments.

   :param args: arguments to add to set
   :param nargs: number of arguments
   :return: new set containing args
*/
PyObject *KosUtils_BuildSet(PyObject *const *args, Py_ssize_t nargs);


/**
   Chained getattr through a sequence of attribute names.

   Walks element through each name in attrlist left to right. When
   default_val is non-NULL, return it on AttributeError; otherwise
   propagate AttributeError. Other exceptions always propagate.

   :param element: starting object
   :param attrlist: sequence of attribute names
   :param default_val: default value or NULL when omitted
   :return: final attribute value or default_val
   :raises ValueError: when attrlist is empty
*/
PyObject *KosUtils_GetAttrStar(
    PyObject *element,
    PyObject *attrlist,
    PyObject *default_val);


/**
   Chained hasattr through a sequence of attribute names.

   Walks element through each name in attrlist left to right. Returns
   Py_True only when every step succeeds; Py_False on the first failed
   lookup (any exception cleared, Python hasattr semantics per step).

   :param element: starting object
   :param attrlist: sequence of attribute names
   :return: Py_True or Py_False
   :raises ValueError: when attrlist is empty
*/
PyObject *KosUtils_HasAttrStar(
    PyObject *element,
    PyObject *attrlist);


/**
   Chained getitem through a sequence of keys.

   Walks element through each key in keylist left to right. When
   default_val is non-NULL, return it on LookupError; otherwise
   propagate LookupError. Other exceptions always propagate.

   :param element: starting object
   :param keylist: sequence of keys
   :param default_val: default value or NULL when omitted
   :return: final item value or default_val
   :raises ValueError: when keylist is empty
*/
PyObject *KosUtils_GetItemStar(
    PyObject *element,
    PyObject *keylist,
    PyObject *default_val);


/**
   Chained hasitem through a sequence of keys.

   Walks element through each key in keylist left to right. Returns
   Py_True only when every step succeeds; Py_False on the first failed
   lookup (any exception cleared).

   :param element: starting object
   :param keylist: sequence of keys
   :return: Py_True or Py_False
   :raises ValueError: when keylist is empty
*/
PyObject *KosUtils_HasItemStar(
    PyObject *element,
    PyObject *keylist);


/**
   Build a dict from key-value pair arguments.

   Each positional argument should be an iterable of length 2 (key, value).
   Pairs are automatically unpacked. Keyword arguments are merged in.

   :param args: tuple of (key, value) pairs
   :param kwargs: additional keyword arguments to merge
   :return: new dict containing all pairs
*/
PyObject *KosUtils_BuildDict(PyObject *args, PyObject *kwargs);

/**
   Build a class using Koskari class-construction semantics.

   :param name: class name
   :param bases: base classes tuple
   :param namespace: mapping used as class namespace
   :param metaclass: explicit metaclass or NULL
   :return: new class object
*/
PyObject *KosUtils_BuildClass(
    PyObject *name,
    PyObject *bases,
    PyObject *namespace,
    PyObject *metaclass);


/* === unit lifecycle === */


int koskari_utils_init(PyObject *mod);
int koskari_utils_clear(PyObject *mod);


#endif


// The end.
