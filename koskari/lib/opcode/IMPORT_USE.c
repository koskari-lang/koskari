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
   IMPORT_USE: arg = extra index. Schema CONST (module name str, or
   None for a bare relative import), COUNT (level: leading dots, 0 =
   absolute), CONST (tuple of feature stems), CONST (tuple of bind
   stems, parallel to the feature stems).

   The feature-family member of the use form. Each stem names a
   feature: the handler probes the stem itself and its compile/ and
   layout/ facet spellings on the imported module, and stores each hit
   under the parallel bind stem with the same facet prefix applied --
   so a rename is pure stem substitution across the family. A stem
   that already carries a facet prefix is literal: one candidate, one
   bind. Per-facet misses are tolerated; a stem resolving no candidate
   at all raises ImportError naming the spellings tried.

   The bare-stem candidate keeps IMPORT_FROM's submodule fallback via
   kos_import_from_member (visible in this translation unit), so a
   stem naming a not-yet-imported submodule behaves as from-import
   does. On a Python module, whose namespace carries no facet keys,
   the whole opcode degrades to from-import behavior per stem.
*/


static const char *_use_facet_prefixes[] = { "compile/", "layout/", NULL };


/**
   True when the stem string already carries one of the facet
   prefixes, making it a literal single-binding member.
*/
static int kos_use_stem_is_literal(const char *stem) {
  const char **pre;

  for (pre = _use_facet_prefixes; *pre; pre++) {
    if (strncmp(stem, *pre, strlen(*pre)) == 0)
      return 1;
  }
  return 0;
}


KOS_OP_HANDLER_COLD(IMPORT_USE) {
  KosCode *code = KosEvalState_CurrentCode(es);
  KosExtra row = KosCode_GetExtra(code, KosOp_ARG(insn));

  PyObject *name = KosEvalState_CONST(es, row.a0);
  if (! name)
    return EVAL_RAISE();

  PyObject *stems = KosEvalState_CONST(es, row.a2);
  if (! stems)
    return EVAL_RAISE();

  PyObject *binds = KosEvalState_CONST(es, row.a3);
  if (! binds)
    return EVAL_RAISE();

  if (! PyTuple_CheckExact(stems) || ! PyTuple_CheckExact(binds) ||
      PyTuple_GET_SIZE(stems) != PyTuple_GET_SIZE(binds)) {
    PyErr_SetString(PyExc_TypeError, "IMPORT_USE names must be parallel"
                    " tuples");
    return EVAL_RAISE();
  }

  PyObject *mod = kos_import_module(es, name, (long) row.a1, 1);
  if (! mod)
    return EVAL_RAISE();

  Py_ssize_t n = PyTuple_GET_SIZE(stems);
  for (Py_ssize_t i = 0; i < n; i++) {
    PyObject *stem = PyTuple_GET_ITEM(stems, i);
    PyObject *bind = PyTuple_GET_ITEM(binds, i);

    const char *rawstem = PyUnicode_Check(stem)?
      PyUnicode_AsUTF8(stem): NULL;
    if (! rawstem) {
      if (! PyErr_Occurred())
        PyErr_Format(PyExc_TypeError, "invalid use stem %R", stem);
      Py_DECREF(mod);
      return EVAL_RAISE();
    }

    int hits = 0;

    /* the bare candidate, with from-import's submodule fallback */
    PyObject *val = kos_import_from_member(mod, stem);
    if (val) {
      int stored = frame_ns_store(es, bind, val);
      Py_DECREF(val);
      if (stored < 0) {
        Py_DECREF(mod);
        return EVAL_RAISE();
      }
      hits++;

    } else if (PyErr_ExceptionMatches(PyExc_ImportError) &&
               ! PyErr_ExceptionMatches(PyExc_ModuleNotFoundError)) {
      /* the member-missing report crafted by kos_import_from_member;
         a ModuleNotFoundError is a real failure inside a submodule's
         own import and must propagate, matching IMPORT_FROM */
      PyErr_Clear();

    } else {
      Py_DECREF(mod);
      return EVAL_RAISE();
    }

    /* facet candidates, unless the stem is already a literal facet */
    if (! kos_use_stem_is_literal(rawstem)) {
      const char **pre;

      for (pre = _use_facet_prefixes; *pre; pre++) {
        PyObject *attr = PyUnicode_FromFormat("%s%U", *pre, stem);
        if (! attr) {
          Py_DECREF(mod);
          return EVAL_RAISE();
        }

        val = PyObject_GetAttr(mod, attr);
        Py_DECREF(attr);
        if (! val) {
          if (! PyErr_ExceptionMatches(PyExc_AttributeError)) {
            Py_DECREF(mod);
            return EVAL_RAISE();
          }
          PyErr_Clear();
          continue;
        }

        PyObject *bound = PyUnicode_FromFormat("%s%U", *pre, bind);
        if (! bound) {
          Py_DECREF(val);
          Py_DECREF(mod);
          return EVAL_RAISE();
        }

        int stored = frame_ns_store(es, bound, val);
        Py_DECREF(bound);
        Py_DECREF(val);
        if (stored < 0) {
          Py_DECREF(mod);
          return EVAL_RAISE();
        }
        hits++;
      }
    }

    if (! hits) {
      PyErr_Format(PyExc_ImportError,
                   "module %R has no feature %U"
                   " (tried %U, compile/%U, layout/%U)",
                   mod, stem, stem, stem, stem);
      Py_DECREF(mod);
      return EVAL_RAISE();
    }
  }
  Py_DECREF(mod);

  return EVAL_NORMAL();
}


// The end.
