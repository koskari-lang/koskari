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
   koskari/lib/mark.h

   Mark: a symbol or keyword a macro template introduced, boxed with
   the expansions that introduced it. The compiler declares and
   resolves a marked identifier under its mangled key, so a name a
   template binds never collides with the same name at the use site.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#ifndef KOSKARI_MARK_H
#define KOSKARI_MARK_H


#include <Python.h>


typedef struct KosMark {
  PyObject_HEAD
  PyObject *datum;   /* interned symbol or keyword */
  PyObject *marks;   /* tuple of int, outermost expansion first */
  PyObject *key;     /* mangled name: str(datum) + "#" + hex per mark */
} KosMark;


extern PyTypeObject KosMarkType;


#define KosMark_Check(obj)                                      \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosMarkType))

#define KosMark_DATUM(m) (((KosMark *) (m))->datum)
#define KosMark_MARKS(m) (((KosMark *) (m))->marks)
#define KosMark_KEY(m) (((KosMark *) (m))->key)


/**
   New Mark over datum, a symbol or keyword, with marks a non-empty
   tuple of ints. New reference, or NULL with an exception set.
*/
PyObject *KosMark_New(PyObject *datum, PyObject *marks);


int koskari_mark_init(PyObject *mod);
int koskari_mark_clear(PyObject *mod);


#endif /* KOSKARI_MARK_H */


// The end.
