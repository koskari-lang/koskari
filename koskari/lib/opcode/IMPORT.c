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
   IMPORT: arg = constant index (str).  Perform an absolute module import
   and push the leaf module onto the stack.

   The constant at arg must be a str naming the module, e.g. "foo" or
   "foo.bar".  For dotted names, the leaf module is pushed (foo.bar is
   pushed, not foo).  This matches importlib.import_module semantics and
   differs from bare __import__() which returns the topmost package.

   The leaf behaviour is achieved by passing the module-level sentinel
   _import_fromlist ("*",) as fromlist to PyImport_ImportModuleLevelObject,
   which causes Python to return the deepest component rather than the root.
   _import_fromlist is initialised once in koskari_eval_init.

   Equivalent Python:
       import importlib
       importlib.import_module("foo.bar")   # returns foo.bar, not foo
*/

KOS_OP_HANDLER(IMPORT) {
  PyObject *name = get_name_from_insn(CURRENT_CODE(ctx), insn);
  if (! name)
    return KOS_OP_POST_ERR();

  PyObject *mod = PyImport_ImportModuleLevelObject(
    name, NULL, NULL, _import_fromlist, 0);
  if (! mod)
    return KOS_OP_POST_ERR();

  return kos_op_outcome_from_int_step(push_result(ctx, mod));
}
