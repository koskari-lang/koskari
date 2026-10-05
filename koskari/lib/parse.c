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
   BaseSourceStream: native source stream for the parser

   Character-level I/O with position tracking over a Python file-like
   source, for the Koskari s-expression reader.

   :author: Christopher O'Brien <obriencj@gmail.com>
   :license: GNU General Public License v3
*/


#include "lib.h"


/* === string constants === */


static PyObject *_str_read = NULL;
static PyObject *_str_seek = NULL;
static PyObject *_str_tell = NULL;
static PyObject *_long_one = NULL;


/* === helper macros === */


#define STREAM_STREAM(s) (((KosBaseSourceStream *)(s))->stream)
#define STREAM_FILENAME(s) (((KosBaseSourceStream *)(s))->filename)
#define STREAM_LINE(s) (((KosBaseSourceStream *)(s))->line)
#define STREAM_COL(s) (((KosBaseSourceStream *)(s))->col)


/* === position tracking === */


static void update_position(
    KosBaseSourceStream *self,
    PyObject *data) {
  /**
     Update line and column based on Unicode characters read

     Iterates over Unicode code points (not UTF-8 bytes) to correctly
     handle multi-byte characters.

     Newline (\n) increments line and resets column.
     Carriage return (\r) resets column only.
     Other characters increment column.
  */

  Py_ssize_t len = PyUnicode_GET_LENGTH(data);
  Py_ssize_t line = self->line;
  Py_ssize_t col = self->col;

  for (Py_ssize_t i = 0; i < len; i++) {
    Py_UCS4 ch = PyUnicode_READ_CHAR(data, i);
    if (ch == '\n') {
      line++;
      col = 0;
    } else if (ch == '\r') {
      col = 0;
    } else {
      col++;
    }
  }

  self->line = line;
  self->col = col;
}


/* === stream operations === */


static PyObject *stream_read(PyObject *stream, Py_ssize_t count) {
  /**
     Read count characters from stream
  */

  PyObject *count_obj = PyLong_FromSsize_t(count);
  if (! count_obj)
    return NULL;

  PyObject *result = PyObject_CallMethodObjArgs(
          stream, _str_read, count_obj, NULL);

  Py_DECREF(count_obj);
  return result;
}


static Py_ssize_t stream_tell(PyObject *stream) {
  /**
     Get current stream position
  */

  PyObject *pos = PyObject_CallMethodObjArgs(stream, _str_tell, NULL);
  if (! pos)
    return -1;

  Py_ssize_t result = PyLong_AsSsize_t(pos);
  Py_DECREF(pos);
  return result;
}


static int stream_seek(PyObject *stream, Py_ssize_t pos) {
  /**
     Seek to position in stream
  */

  PyObject *pos_obj = PyLong_FromSsize_t(pos);
  if (! pos_obj)
    return -1;

  PyObject *result = PyObject_CallMethodObjArgs(
          stream, _str_seek, pos_obj, NULL);

  Py_DECREF(pos_obj);

  if (! result)
    return -1;

  Py_DECREF(result);
  return 0;
}


/* === BaseSourceStream === */


static PyObject *bss_new(
    PyTypeObject *type,
    PyObject *args,
    PyObject *kwds) {
  /**
    Allocate a new BaseSourceStream with NULL/default fields.
    Actual initialization happens in bss_init.
  */

  KosBaseSourceStream *self = (KosBaseSourceStream *) type->tp_alloc(type, 0);
  if (! self)
    return NULL;

  /* Initialize fields to safe defaults */
  self->stream = NULL;
  self->filename = NULL;
  self->line = 1;
  self->col = 0;

  return (PyObject *) self;
}


static int bss_init(
    PyObject *self,
    PyObject *args,
    PyObject *kwds) {
  /**
    Initialize a BaseSourceStream with stream, filename, and position.
  */

  static char *keywords[] = { "stream", "filename", "line", "col", NULL };

  PyObject *stream = NULL;
  PyObject *filename = NULL;
  Py_ssize_t line = 1;
  Py_ssize_t col = 0;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "O|Onn:BaseSourceStream",
                                    keywords, &stream, &filename,
                                    &line, &col))
    return -1;

  if (! stream) {
    PyErr_SetString(PyExc_TypeError,
                    "BaseSourceStream requires a stream");
    return -1;
  }

  if (! filename) {
    filename = Py_None;
  }

  /* Clear any existing references */
  Py_CLEAR(STREAM_STREAM(self));
  Py_CLEAR(STREAM_FILENAME(self));

  /* Set new values */
  Py_INCREF(stream);
  STREAM_STREAM(self) = stream;

  Py_INCREF(filename);
  STREAM_FILENAME(self) = filename;

  STREAM_LINE(self) = line;
  STREAM_COL(self) = col;

  return 0;
}


PyObject *KosBaseSourceStream_New(
    PyObject *stream,
    PyObject *filename,
    Py_ssize_t line,
    Py_ssize_t col) {
  /**
     Create a new BaseSourceStream. For direct C-level instantiation.
  */

  if (! stream) {
    PyErr_SetString(PyExc_TypeError,
                    "BaseSourceStream requires a stream");
    return NULL;
  }

  if (! filename) {
    filename = Py_None;
  }

  KosBaseSourceStream *self = (KosBaseSourceStream *)
      KosBaseSourceStreamType.tp_alloc(&KosBaseSourceStreamType, 0);
  if (! self)
    return NULL;

  Py_INCREF(stream);
  self->stream = stream;

  Py_INCREF(filename);
  self->filename = filename;

  self->line = line;
  self->col = col;

  return (PyObject *) self;
}


/* 0 when the stream is present, else -1 with ValueError set: an
   instance from a bare __new__ has no stream to read */
static int bss_require_stream(KosBaseSourceStream *s) {
  if (s->stream)
    return 0;
  PyErr_SetString(PyExc_ValueError, "BaseSourceStream is not initialized");
  return -1;
}


static void bss_dealloc(PyObject *self) {
  Py_CLEAR(STREAM_STREAM(self));
  Py_CLEAR(STREAM_FILENAME(self));
  Py_TYPE(self)->tp_free(self);
}


PyObject *KosBaseSourceStream_Read(PyObject *self, Py_ssize_t count) {
  KosBaseSourceStream *s = (KosBaseSourceStream *) self;
  if (bss_require_stream(s))
    return NULL;

  if (count < 1) {
    PyErr_SetString(PyExc_ValueError, "read count must be >= 1");
    return NULL;
  }

  PyObject *data = stream_read(s->stream, count);
  if (! data)
    return NULL;

  if (! PyUnicode_Check(data)) {
    Py_DECREF(data);
    PyErr_SetString(PyExc_TypeError, "stream.read() must return str");
    return NULL;
  }

  update_position(s, data);

  return data;
}


static PyObject *m_bss_read(PyObject *self, PyObject *args) {
  Py_ssize_t count = 1;

  if (! PyArg_ParseTuple(args, "|n:read", &count))
    return NULL;

  return KosBaseSourceStream_Read(self, count);
}


PyObject *KosBaseSourceStream_Peek(PyObject *self, Py_ssize_t count) {
  KosBaseSourceStream *s = (KosBaseSourceStream *) self;
  if (bss_require_stream(s))
    return NULL;

  if (count < 1) {
    PyErr_SetString(PyExc_ValueError, "peek count must be >= 1");
    return NULL;
  }

  /* save position */
  Py_ssize_t pos = stream_tell(s->stream);
  if (pos < 0)
    return NULL;

  /* read without updating our position */
  PyObject *data = stream_read(s->stream, count);
  if (! data)
    return NULL;

  /* restore position */
  if (stream_seek(s->stream, pos) < 0) {
    Py_DECREF(data);
    return NULL;
  }

  return data;
}


static PyObject *m_bss_peek(PyObject *self, PyObject *args) {
  Py_ssize_t count = 1;

  if (! PyArg_ParseTuple(args, "|n:peek", &count))
    return NULL;

  return KosBaseSourceStream_Peek(self, count);
}


PyObject *KosBaseSourceStream_Position(PyObject *self) {
  KosBaseSourceStream *s = (KosBaseSourceStream *) self;
  return Py_BuildValue("(nn)", s->line, s->col);
}


static PyObject *m_bss_position(PyObject *self, PyObject *_noargs) {
  return KosBaseSourceStream_Position(self);
}


PyObject *KosBaseSourceStream_ReadUntilPred(
    PyObject *self,
    KosCharPredicate pred,
    void *context) {
  /**
     Read characters until predicate returns non-zero.

     The terminating character is not consumed.
     This is the core implementation used by read_until_chars,
     skip_whitespace, and read_until.
  */

  KosBaseSourceStream *s = (KosBaseSourceStream *) self;
  if (bss_require_stream(s))
    return NULL;

  /* save starting position */
  Py_ssize_t start_pos = stream_tell(s->stream);
  if (start_pos < 0)
    return NULL;

  /* scan character by character */
  Py_ssize_t scan_count = 0;

  while (1) {
    PyObject *ch = PyObject_CallMethodObjArgs(s->stream, _str_read, _long_one, NULL);
    if (! ch)
      return NULL;

    if (! PyUnicode_Check(ch) || PyUnicode_GET_LENGTH(ch) == 0) {
      /* EOF reached */
      Py_DECREF(ch);
      break;
    }

    /* get the Unicode code point */
    Py_UCS4 codepoint = PyUnicode_ReadChar(ch, 0);
    Py_DECREF(ch);

    if (codepoint == (Py_UCS4) -1 && PyErr_Occurred())
      return NULL;

    /* check predicate */
    if (pred(codepoint, context))
      break;

    scan_count++;
  }

  /* seek back to start */
  if (stream_seek(s->stream, start_pos) < 0)
    return NULL;

  /* read the counted characters */
  if (scan_count == 0) {
    return PyUnicode_FromString("");
  }

  return KosBaseSourceStream_Read(self, scan_count);
}


PyObject *KosBaseSourceStream_ReadUntil(
    PyObject *self,
    PyObject *testf) {
  /**
     Read characters until a Python callable returns truthy.

     For each character read, calls testf(char) where char is a
     single-character string. Stops when the callable returns truthy.
     The terminating character is not consumed.
  */

  KosBaseSourceStream *s = (KosBaseSourceStream *) self;
  if (bss_require_stream(s))
    return NULL;

  if (! PyCallable_Check(testf)) {
    PyErr_SetString(PyExc_TypeError, "testf must be callable");
    return NULL;
  }

  /* save starting position */
  Py_ssize_t start_pos = stream_tell(s->stream);
  if (start_pos < 0)
    return NULL;

  /* scan character by character */
  Py_ssize_t scan_count = 0;

  while (1) {
    PyObject *ch = PyObject_CallMethodObjArgs(s->stream, _str_read, _long_one, NULL);
    if (! ch)
      return NULL;

    if (! PyUnicode_Check(ch) || PyUnicode_GET_LENGTH(ch) == 0) {
      /* EOF reached */
      Py_DECREF(ch);
      break;
    }

    /* call the test function with the character */
    PyObject *result = PyObject_CallFunctionObjArgs(testf, ch, NULL);
    Py_DECREF(ch);

    if (! result)
      return NULL;

    int is_truthy = PyObject_IsTrue(result);
    Py_DECREF(result);

    if (is_truthy < 0)
      return NULL;

    if (is_truthy)
      break;

    scan_count++;
  }

  /* seek back to start */
  if (stream_seek(s->stream, start_pos) < 0)
    return NULL;

  /* read the counted characters */
  if (scan_count == 0) {
    return PyUnicode_FromString("");
  }

  return KosBaseSourceStream_Read(self, scan_count);
}


static PyObject *m_bss_read_until(PyObject *self, PyObject *testf) {
  return KosBaseSourceStream_ReadUntil(self, testf);
}


/* === read_until_chars predicate === */


typedef struct {
  PyObject *terminators;      /* the terminator string object */
  Py_ssize_t length;          /* cached length */
} CharsPredicateContext;


static int chars_predicate(Py_UCS4 ch, void *ctx) {
  /**
     Predicate that returns non-zero if ch is in the terminator string.
  */

  CharsPredicateContext *context = (CharsPredicateContext *) ctx;
  return PyUnicode_FindChar(context->terminators, ch, 0,
                            context->length, 1) >= 0;
}


PyObject *KosBaseSourceStream_ReadUntilChars(
    PyObject *self,
    PyObject *terminators) {
  /**
     Read characters until a terminating character is found.

     The terminating character is not consumed.
  */

  if (! PyUnicode_Check(terminators)) {
    PyErr_SetString(PyExc_TypeError, "terminators must be str");
    return NULL;
  }

  CharsPredicateContext ctx;
  ctx.terminators = terminators;
  ctx.length = PyUnicode_GET_LENGTH(terminators);

  return KosBaseSourceStream_ReadUntilPred(self, chars_predicate, &ctx);
}


static PyObject *m_bss_read_until_chars(PyObject *self, PyObject *args) {
  PyObject *terminators = NULL;

  if (! PyArg_ParseTuple(args, "O:read_until_chars", &terminators))
    return NULL;

  return KosBaseSourceStream_ReadUntilChars(self, terminators);
}


/* === skip_whitespace predicate === */


static int not_whitespace_predicate(Py_UCS4 ch, void *ctx) {
  /**
     Predicate that returns non-zero if ch is NOT whitespace.
     This stops reading when we hit a non-whitespace character.
  */

  (void) ctx;  /* unused */
  return ! (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r');
}


PyObject *KosBaseSourceStream_SkipWhitespace(PyObject *self) {
  /**
     Skip whitespace characters, returning the skipped text.
  */

  return KosBaseSourceStream_ReadUntilPred(self, not_whitespace_predicate, NULL);
}


static PyObject *m_bss_skip_whitespace(PyObject *self, PyObject *_noargs) {
  return KosBaseSourceStream_SkipWhitespace(self);
}


static PyObject *m_bss_readline(
    PyObject *self,
    PyObject *_noargs) {
  /**
     Read a line from the stream.
  */

  KosBaseSourceStream *s = (KosBaseSourceStream *) self;
  if (bss_require_stream(s))
    return NULL;

  /* save starting position */
  Py_ssize_t start_pos = stream_tell(s->stream);
  if (start_pos < 0)
    return NULL;

  /* scan to newline */
  Py_ssize_t scan_count = 0;

  while (1) {
    PyObject *ch = PyObject_CallMethodObjArgs(s->stream, _str_read, _long_one, NULL);
    if (! ch)
      return NULL;

    if (! PyUnicode_Check(ch) || PyUnicode_GET_LENGTH(ch) == 0) {
      /* EOF reached */
      Py_DECREF(ch);
      break;
    }

    /* the code point, read without encoding: a lone surrogate is a
       character to scan past, not an error */
    Py_UCS4 codepoint = PyUnicode_ReadChar(ch, 0);
    Py_DECREF(ch);

    if (codepoint == (Py_UCS4) -1 && PyErr_Occurred())
      return NULL;

    scan_count++;
    if (codepoint == '\n')
      break;
  }

  /* seek back to start */
  if (stream_seek(s->stream, start_pos) < 0)
    return NULL;

  /* read the counted characters */
  if (scan_count == 0) {
    return PyUnicode_FromString("");
  }

  return KosBaseSourceStream_Read(self, scan_count);
}


/* === checkpoint === */


static PyObject *m_bss_mark(PyObject *self, PyObject *_noargs) {
  /**
     Capture a checkpoint of the current stream state.

     Returns an opaque (tell, line, col) tuple that rewind() restores.
     Speculative reads may consume freely and then rewind to this mark.
  */

  KosBaseSourceStream *s = (KosBaseSourceStream *) self;
  if (bss_require_stream(s))
    return NULL;

  Py_ssize_t pos = stream_tell(s->stream);
  if (pos < 0)
    return NULL;

  return Py_BuildValue("(nnn)", pos, s->line, s->col);
}


static PyObject *m_bss_rewind(PyObject *self, PyObject *mark) {
  /**
     Restore the stream state captured by a prior mark().
  */

  KosBaseSourceStream *s = (KosBaseSourceStream *) self;
  if (bss_require_stream(s))
    return NULL;

  Py_ssize_t pos, line, col;
  if (! PyArg_ParseTuple(mark, "nnn", &pos, &line, &col))
    return NULL;

  if (stream_seek(s->stream, pos) < 0)
    return NULL;

  s->line = line;
  s->col = col;

  Py_RETURN_NONE;
}


/* === getters === */


static PyObject *bss_get_stream(PyObject *self, void *closure) {
  PyObject *stream = STREAM_STREAM(self);
  return Py_NewRef(stream ? stream : Py_None);
}


static PyObject *bss_get_filename(PyObject *self, void *closure) {
  PyObject *filename = STREAM_FILENAME(self);
  return Py_NewRef(filename ? filename : Py_None);
}


static PyObject *bss_get_line(PyObject *self, void *closure) {
  return PyLong_FromSsize_t(STREAM_LINE(self));
}


static PyObject *bss_get_col(PyObject *self, void *closure) {
  return PyLong_FromSsize_t(STREAM_COL(self));
}


static PyGetSetDef bss_getset[] = {
  { "stream", bss_get_stream, NULL,
    "The underlying stream object", NULL },

  { "filename", bss_get_filename, NULL,
    "The filename for error reporting", NULL },

  { "lin", bss_get_line, NULL,
    "The current line number (1-based)", NULL },

  { "col", bss_get_col, NULL,
    "The current column number (0-based)", NULL },

  { NULL, NULL, NULL, NULL, NULL },
};


/* === methods === */


static PyMethodDef bss_methods[] = {
  { "read", (PyCFunction) m_bss_read, METH_VARARGS,
    "read(count=1) -> str\n"
    "Read count characters from the stream, tracking position." },

  { "peek", (PyCFunction) m_bss_peek, METH_VARARGS,
    "peek(count=1) -> str\n"
    "Peek at count characters without consuming them." },

  { "position", (PyCFunction) m_bss_position, METH_NOARGS,
    "position() -> (line, col)\n"
    "Get the current position in the stream." },

  { "read_until", (PyCFunction) m_bss_read_until, METH_O,
    "read_until(testf) -> str\n"
    "Read until testf(char) returns True.\n"
    "The terminating character is not consumed." },

  { "read_until_chars", (PyCFunction) m_bss_read_until_chars, METH_VARARGS,
    "read_until_chars(terminators) -> str\n"
    "Read characters until a terminating character is found.\n"
    "The terminating character is not consumed." },

  { "skip_whitespace", (PyCFunction) m_bss_skip_whitespace, METH_NOARGS,
    "skip_whitespace() -> str\n"
    "Skip whitespace characters, returning the skipped text." },

  { "readline", (PyCFunction) m_bss_readline, METH_NOARGS,
    "readline() -> str\n"
    "Read a line from the stream including the newline." },

  { "mark", (PyCFunction) m_bss_mark, METH_NOARGS,
    "mark() -> checkpoint\n"
    "Capture an opaque checkpoint of the current stream state." },

  { "rewind", (PyCFunction) m_bss_rewind, METH_O,
    "rewind(checkpoint) -> None\n"
    "Restore the stream state captured by a prior mark()." },

  { NULL, NULL, 0, NULL },
};


/* === type object === */


PyTypeObject KosBaseSourceStreamType = {
  PyVarObject_HEAD_INIT(NULL, 0)

  "BaseSourceStream",
  sizeof(KosBaseSourceStream),
  0,

  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
  .tp_doc = "Native base class for source stream with position tracking.",
  .tp_methods = bss_methods,
  .tp_getset = bss_getset,
  .tp_new = bss_new,
  .tp_init = bss_init,
  .tp_dealloc = bss_dealloc,
};


/* === unit lifecycle === */


int koskari_parse_init(PyObject *mod) {

  PyObject *dict = PyModule_GetDict(mod);
  if (! dict)
    return -1;

  /* initialize string constants */
  STR_CONST(_str_read, "read");
  STR_CONST(_str_seek, "seek");
  STR_CONST(_str_tell, "tell");

  /* initialize integer constants */
  if (! _long_one) {
    _long_one = PyLong_FromLong(1);
    if (! _long_one)
      return -1;
  }

  /* initialize types */
  if (PyType_Ready(&KosBaseSourceStreamType))
    return -1;

  /* add types to module dict */
  if (PyDict_SetItemString(dict, "BaseSourceStream",
                           (PyObject *) &KosBaseSourceStreamType))
    return -1;

  return 0;
}


int koskari_parse_clear(PyObject *mod) {

  /* clear string constants */
  Py_CLEAR(_str_read);
  Py_CLEAR(_str_seek);
  Py_CLEAR(_str_tell);

  /* clear integer constants */
  Py_CLEAR(_long_one);

  return 0;
}


// The end.
