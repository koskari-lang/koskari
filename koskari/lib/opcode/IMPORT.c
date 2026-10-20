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
  IMPORT: arg = extra index. Schema CONST (module name str, or None for
  a bare relative import), COUNT (level: leading dots, 0 = absolute),
  COUNT (leaf flag: 0 = bind root, 1 = bind leaf), CONST (binding name
  str).

  Self-contained: imports the module via kos_import_module and stores
  it into the current frame namespace under the binding name. No stack
  effect.

  With the leaf flag clear the root package is bound while the full
  dotted chain is still imported (plain Python "import foo.bar"
  semantics); with it set the deepest submodule is bound (the
  "(import [foo.bar as: fbar])" member form).
*/
KOS_OP_HANDLER(IMPORT) {
  KOS_SYNC_PC(ctx, insn);
  const KosExtra *row = KosOp_ROW(insn);

  PyObject *name = KosExtra_OBJ(row, 0);

  PyObject *bind = KosExtra_OBJ(row, 3);

  PyObject *mod = kos_import_module(ctx, name, (long) KosExtra_IMM(row, 1), KosExtra_IMM(row, 2));
  if (unlikely(! mod))
    RESULT_RAISE();

  if (unlikely(frame_ns_store(ctx, bind, mod) < 0)) {
    Py_DECREF(mod);
    RESULT_RAISE();
  }
  Py_DECREF(mod);

  RESULT_NEXT(ctx);
}


// The end.
