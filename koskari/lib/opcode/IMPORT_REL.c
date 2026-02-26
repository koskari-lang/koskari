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
   IMPORT_REL: arg = constant index.  Perform a relative module import
   and push the leaf module onto the stack.

   The constant at arg must be a 2-tuple (level:int, name:str|None).
   level is the number of leading dots in the original import expression
   (e.g. 1 for "(import .foo)", 2 for "(import ..foo)").  name is the
   module name after the dots, or None for a bare relative import
   (e.g. "(import .)" which imports the enclosing package itself).

   The globals namespace of the outermost frame is passed to
   PyImport_ImportModuleLevelObject so that Python can read __name__ and
   __package__ to resolve the anchor package.  This is the same namespace
   consulted by Python's own relative import mechanism.

   Like IMPORT, the leaf module is pushed (not the root package) by
   passing _import_fromlist as the fromlist sentinel.

   _import_fromlist is initialised once in koskari_eval_init.
*/

/* _import_fromlist is defined and initialised in eval.c */
extern PyObject *_import_fromlist;


KOS_OP_HANDLER(IMPORT_REL) {
  /* Constant is a (level:int, name:str|None) tuple */
  PyObject *tup = get_name_from_insn(CURRENT_CODE(ctx), insn);
  if (! tup)
    return -1;

  if (! PyTuple_Check(tup) || PyTuple_GET_SIZE(tup) != 2) {
    PyErr_SetString(PyExc_TypeError,
                    "IMPORT_REL constant must be a (level, name) tuple");
    return -1;
  }

  PyObject *level_obj = PyTuple_GET_ITEM(tup, 0);
  PyObject *name      = PyTuple_GET_ITEM(tup, 1);

  long level = PyLong_AsLong(level_obj);
  if (level < 0 && PyErr_Occurred())
    return -1;

  /* A bare relative import "(import .)" has name == None */
  if (name == Py_None)
    name = NULL;

  /* Walk the dynamic chain to find the globals (outermost) frame namespace.
     This is the same logic as the GLOBALS opcode; the globals namespace
     provides __name__ and __package__ for relative import anchor resolution. */
  if (KosContext_CHAIN_VERSION(ctx) != KosContext_GLOBALS_CACHE_VERSION(ctx)) {
    KosDynNode *n = (KosDynNode *) CURRENT_FRAME(ctx);
    KosDynFrame *outer = CURRENT_FRAME(ctx);
    while (n && n->parent) {
      n = n->parent;
      if (KosDynNode_IS_FRAME(n))
        outer = (KosDynFrame *) n;
    }
    KosContext_GLOBALS_FRAME(ctx) = outer;
    KosContext_GLOBALS_CACHE_VERSION(ctx) = KosContext_CHAIN_VERSION(ctx);
  }

  PyObject *globals_ns = KosDynFrame_NS(KosContext_GLOBALS_FRAME(ctx));

  PyObject *mod = PyImport_ImportModuleLevelObject(
    name, globals_ns, NULL, _import_fromlist, (int) level);
  if (! mod)
    return -1;

  return push_result(ctx, mod);
}
