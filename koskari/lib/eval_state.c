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
   koskari/lib/eval_state.c

   Thin KosEvalState accessors for suspension tagging, Python await/resume
   bridge fields, and domain TLS scope.  Delegates to KosContext; see
   design/kos_eval_state_api_migration.md

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
   :ai-assistant: Auto via Cursor
*/


#include "lib.h"


void KosEvalState_SetSuspensionEvent(KosEvalState *es, uint8_t ev) {

  KosContext_SUSPENSION_EVENT(KosEvalState_CONTEXT(es)) = ev;
}


void KosEvalState_ClearSuspensionEvent(KosEvalState *es) {

  KosContext_SUSPENSION_EVENT(KosEvalState_CONTEXT(es)) = KOS_SUSP_NONE;
}


uint8_t KosEvalState_SuspensionEvent(const KosEvalState *es) {

  return KosContext_SUSPENSION_EVENT(KosEvalState_CONTEXT(es));
}


void KosEvalState_PythonAwaitSetIter(KosEvalState *es, PyObject *iter) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  Py_XDECREF(KosContext_PY_AWAIT_ITER(ctx));
  KosContext_PY_AWAIT_ITER(ctx) = iter;
}


void KosEvalState_PythonAwaitClearIter(KosEvalState *es) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  Py_CLEAR(KosContext_PY_AWAIT_ITER(ctx));
}


PyObject *KosEvalState_PythonAwaitIter(KosEvalState *es) {

  return KosContext_PY_AWAIT_ITER(KosEvalState_CONTEXT(es));
}


void KosEvalState_PythonResumeSet(KosEvalState *es, PyObject *val, uint8_t kind) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  Py_XDECREF(KosContext_PY_RESUME_INJECT(ctx));
  KosContext_PY_RESUME_INJECT(ctx) = val;
  KosContext_PY_RESUME_KIND(ctx) = kind;
}


int KosEvalState_PythonResumeTake(
    KosEvalState *es,
    PyObject **out_val,
    uint8_t *out_kind) {

  KosContext *ctx = KosEvalState_CONTEXT(es);

  if (out_val)
    *out_val = KosContext_PY_RESUME_INJECT(ctx);
  if (out_kind)
    *out_kind = KosContext_PY_RESUME_KIND(ctx);
  KosContext_PY_RESUME_INJECT(ctx) = NULL;
  return (out_val && *out_val != NULL) ? 1 : 0;
}


void KosEvalState_EnterDomainScope(KosEvalState *es) {

  kos_domain_push_scope(KosEvalState_CONTEXT(es));
}


void KosEvalState_LeaveDomainScope(KosEvalState *es) {

  kos_domain_pop_scope(KosEvalState_CONTEXT(es));
}
