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


#ifndef KOSKARI_PARSE_H
#define KOSKARI_PARSE_H


#include <Python.h>


/* === BaseSourceStream structure === */


typedef struct KosBaseSourceStream {
  PyObject_HEAD

  PyObject *stream;       /* underlying seekable text stream */
  PyObject *filename;     /* filename for error reporting */
  Py_ssize_t line;        /* current line number (1-based) */
  Py_ssize_t col;         /* current column number (0-based) */
} KosBaseSourceStream;


/* === type objects === */


extern PyTypeObject KosBaseSourceStreamType;


/* === type check macros === */


#define KosBaseSourceStream_Check(obj)                                  \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosBaseSourceStreamType))

#define KosBaseSourceStream_CheckExact(obj)                     \
  ((obj) && ((obj)->ob_type == &KosBaseSourceStreamType))


/* === character predicate === */


/**
   Predicate function type for read_until operations.

   Returns non-zero if the character is a terminator (stop reading).
   The context pointer allows passing additional state to the predicate.
*/
typedef int (*KosCharPredicate)(Py_UCS4 ch, void *context);


/* === public API === */


PyObject *KosBaseSourceStream_New(
        PyObject *stream,
        PyObject *filename,
        Py_ssize_t line,
        Py_ssize_t col);

PyObject *KosBaseSourceStream_Read(PyObject *self, Py_ssize_t count);

PyObject *KosBaseSourceStream_Peek(PyObject *self, Py_ssize_t count);

PyObject *KosBaseSourceStream_Position(PyObject *self);

PyObject *KosBaseSourceStream_ReadUntilPred(
        PyObject *self,
        KosCharPredicate pred,
        void *context);

PyObject *KosBaseSourceStream_ReadUntil(
        PyObject *self,
        PyObject *testf);

PyObject *KosBaseSourceStream_ReadUntilChars(
        PyObject *self,
        PyObject *terminators);

PyObject *KosBaseSourceStream_SkipWhitespace(PyObject *self);


/* === unit lifecycle === */


int koskari_parse_init(PyObject *mod);
int koskari_parse_clear(PyObject *mod);


#endif


// The end.
