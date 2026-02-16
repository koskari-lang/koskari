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

} KosFormalsSpec;


PyObject *KosFormalsSpec_New(
    PyObject *pos,
    PyObject *pos_only,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *star,
    PyObject *starstar);


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

} KosFormals;


PyObject *KosFormals_New(PyObject *spec, PyObject *defaults);


PyObject *KosFormals_NewFull(
    PyObject *pos,
    PyObject *pos_only,
    PyObject *kwd,
    PyObject *kwd_only,
    PyObject *star,
    PyObject *starstar,
    PyObject *defaults);


int KosFormals_Apply(
    PyObject *self,
    PyObject *namespace,
    PyObject *args,
    PyObject *kwds);


int KosFormals_VectorApply(
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

#define KosFormals_Check(obj)                          \
  ((obj) && ((obj)->ob_type == &KosFormalsType))


/* === Unit lifecycle === */


int koskari_formals_init(PyObject *mod);
int koskari_formals_clear(PyObject *mod);


#endif


// The end.
