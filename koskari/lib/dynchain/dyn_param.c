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
  Included by dynchain.c. The KOS_DYN_PARAM node: a parameterize extent
  and the cells it swaps into the context.

  :author: Christopher O'Brien <obriencj@gmail.com>
  :license: GNU General Public License v3
*/

/* === KosDynParam implementation === */


/* Sentinel for "no cell was present" in saved dict; must not be a valid
   cell. Initialized in koskari_dynchain_init, cleared in
   koskari_dynchain_clear. */
static PyObject *_param_sentinel_unset = NULL;


/**
   Build PARAM node from a name -> value bindings dict (borrowed): each
   value is wrapped in a fresh cell (KosRef), so the node's BINDINGS dict
   maps name -> cell.
*/
static KosDynParam *kos_dynparam_new_from_bindings(
    KosDynNode *parent,
    PyObject *bindings) {

  PyObject *cells = PyDict_New();
  if (! cells)
    return NULL;

  {
    PyObject *key;
    PyObject *value;
    Py_ssize_t pos = 0;

    while (PyDict_Next(bindings, &pos, &key, &value)) {
      PyObject *cell = KosSymbolicRef_New(key, value);
      if (! cell) {
        Py_DECREF(cells);
        return NULL;
      }
      if (PyDict_SetItem(cells, key, cell) < 0) {
        Py_DECREF(cell);
        Py_DECREF(cells);
        return NULL;
      }
      Py_DECREF(cell);
    }
  }

  KosDynParam *node = (KosDynParam *) kos_dynnode_alloc();
  if (! node) {
    Py_DECREF(cells);
    return NULL;
  }

  kos_dynnode_init(
    (KosDynNode *) node,
    KOS_DYN_PARAM,
    parent,
    KOS_DYN_CAP_NEEDS_INSTALL_WIND);

  KosDynParam_BINDINGS(node) = cells;
  KosDynParam_SAVED(node) = NULL;

  return node;
}


/**
   PyDict_Copy bindings for INSTALL/PUSH segments. The copy is of cell
   pointers: a clone shares its cells with the source node, so multi-shot
   continuation applications share cells (a set! in one application is
   visible in the next). This is the deliberate Racket-cell semantics; see
   design/archive/domain-task-isolation.md D4.
*/
static KosDynParam *kos_dynparam_clone(KosDynParam *node) {

  if (! node)
    return NULL;

  PyObject *bindings = KosDynParam_BINDINGS(node);
  PyObject *copy = PyDict_Copy(bindings);
  if (! copy)
    return NULL;

  KosDynParam *clone = (KosDynParam *) kos_dynnode_clone_base(
      (KosDynNode *) node);
  if (! clone) {
    Py_DECREF(copy);
    return NULL;
  }

  KosDynParam_BINDINGS(clone) = copy;
  KosDynParam_SAVED(clone) = NULL;

  return clone;
}


/**
   DECREF bindings and saved; return to freelist.
*/
static void kos_dynparam_free(KosDynParam *node) {

  if (! node)
    return;

  Py_XDECREF(KosDynParam_BINDINGS(node));
  KosDynParam_BINDINGS(node) = NULL;
  Py_XDECREF(KosDynParam_SAVED(node));
  KosDynParam_SAVED(node) = NULL;

  kos_dynnode_return_to_freelist((KosDynNode *) node);
}


static int dynnode_to_dict_param(KosDynNode *node, PyObject *data) {

  if (snapshot_set_obj(data, "bindings",
      KosDynParam_BINDINGS((KosDynParam *) node)) < 0)
    return -1;
  if (snapshot_set_obj(data, "saved",
      KosDynParam_SAVED((KosDynParam *) node)) < 0)
    return -1;
  return 0;
}


/**
   Swap the prior cells out of the SAVED dict back into the cell store
   (helper for restore and for enter's error rollback). A sentinel entry
   means no cell was present: remove the key.
*/
static void kos_dynparam_swap_back(PyObject *cells, PyObject *saved) {

  PyObject *sentinel = _param_sentinel_unset;
  PyObject *key;
  PyObject *old_cell;
  Py_ssize_t pos = 0;

  while (PyDict_Next(saved, &pos, &key, &old_cell)) {
    if (old_cell == sentinel) {
      if (PyDict_DelItem(cells, key) < 0)
        PyErr_Clear();
    } else {
      if (PyDict_SetItem(cells, key, old_cell) < 0)
        PyErr_Clear();
    }
  }
}


/**
   Install the node's cells into the context's cell store; stash the
   prior cells (or the absent-sentinel) in SAVED. On failure, roll back
   any swaps already made and leave the store as found.
*/
static int kos_dynparam_enter(KosContext *ctx, KosDynParam *node) {

  PyObject *cells = kos_context_param_refs(ctx);
  if (! cells)
    return -1;

  PyObject *bindings = KosDynParam_BINDINGS(node);
  PyObject *saved = PyDict_New();
  if (! saved)
    return -1;

  PyObject *sentinel = _param_sentinel_unset;
  if (! sentinel) {
    Py_DECREF(saved);
    return -1;
  }

  PyObject *key;
  PyObject *cell;
  Py_ssize_t pos = 0;

  while (PyDict_Next(bindings, &pos, &key, &cell)) {
    PyObject *prior = PyDict_GetItem(cells, key);  /* borrowed */
    PyObject *to_save = prior ? prior : sentinel;
    if (PyDict_SetItem(saved, key, to_save) < 0)
      goto rollback;
    if (PyDict_SetItem(cells, key, cell) < 0)
      goto rollback;
  }

  Py_XSETREF(KosDynParam_SAVED(node), saved);
  return 0;

 rollback:
  {
    /* undo the swaps recorded so far; preserve the pending exception
       across the best-effort dict operations */
    PyObject *typ, *val, *tb;
    PyErr_Fetch(&typ, &val, &tb);
    kos_dynparam_swap_back(cells, saved);
    PyErr_Restore(typ, val, tb);
  }
  Py_DECREF(saved);
  return -1;
}


/**
   Restore the prior cells from SAVED (unwind / transfer).
*/
static void kos_dynparam_restore(KosContext *ctx, KosDynParam *node) {

  PyObject *saved = KosDynParam_SAVED(node);
  if (! saved)
    return;

  PyObject *cells = KosContext_PARAM_REFS(ctx);
  if (! cells)
    return;

  kos_dynparam_swap_back(cells, saved);
}


/**
   INSTALL wind: enter bindings. Callers: kos_dyn_wind, install wind span.
*/
int kos_dyn_param_wind(KosEvalState *es, KosDynParam *node) {
  return kos_dynparam_enter(KosEvalState_CONTEXT(es), node);
}


/**
   Restore outer param values on extent unwind. Callers:
   kos_dyn_extent_freeze and kos_dyn_uninstall_handler.
*/
void kos_dyn_param_unwind(KosEvalState *es, KosDynParam *node) {
  if (KOS_XFER_TRACE)
    SAYF("[xfer] param unwind node=%p", (void *) node);
  kos_dynparam_restore(KosEvalState_CONTEXT(es), node);
}


/**
   Push PARAM from a name -> value dict (borrowed): enter then link as
   DYN_TOP. Callers: opcode PUSH_PARAM.
*/
KosDynParam *kos_dyn_push_param_dict(KosEvalState *es, PyObject *bindings) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (! bindings) {
    PyErr_SetString(PyExc_ValueError, "kos_dyn_push_param_dict: bindings is NULL");
    return NULL;
  }
  if (! PyDict_CheckExact(bindings)) {
    PyErr_Format(PyExc_TypeError, "kos_dyn_push_param_dict expected dict, got %R",
                 bindings);
    return NULL;
  }
  if (PyDict_Size(bindings) == 0) {
    PyErr_SetString(PyExc_ValueError,
                    "KosDynParam requires at least one binding");
    return NULL;
  }

  KosDynParam *node = kos_dynparam_new_from_bindings(
      KosEvalState_DYN_TOP(es), bindings);
  if (! node)
    return NULL;

  if (kos_dynparam_enter(ctx, node) < 0) {
    kos_dynparam_free(node);
    return NULL;
  }

  if (kos_dyn_push(es, (KosDynNode *) node) < 0) {
    kos_dynparam_restore(ctx, node);
    kos_dynparam_free(node);
    return NULL;
  }
  return node;
}


/**
   Restore and unlink PARAM when top. Callers: POP_PARAM, transfer pop.
*/
int kos_dyn_pop_param(KosEvalState *es, KosDynParam *node) {
  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (! KosEvalState_DYN_TOP(es) || KosEvalState_DYN_TOP(es) != (KosDynNode *) node) {
    PyErr_SetString(PyExc_RuntimeError, "PARAM node is not at top of chain");
    return -1;
  }

  KOS_DYN_POP_REQUIRE_KIND(node, KOS_DYN_PARAM, "POP_PARAM", "PARAM");

  kos_dynparam_restore(ctx, node);
  return kos_dyn_pop_transparent_deliver(es, (KosDynNode *) node);
}


/* === GC traversal === */


/**
   GC traverse a KOS_DYN_PARAM: visit the parameter bindings dict and
   the saved bindings from outer scopes.
*/
static int dynnode_traverse_param(KosDynNode *node, visitproc visit, void *arg) {
  KosDynParam *param_node = (KosDynParam *) node;
  Py_VISIT(KosDynParam_BINDINGS(param_node));
  Py_VISIT(KosDynParam_SAVED(param_node));
  return 0;
}


// The end.
