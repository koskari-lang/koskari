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
   tracing.c

   Compile-flag enabled tracing implementation. Counters and report
   builder for freelist, intern, events, and timing features.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "tracing.h"

#include <stddef.h>
#include <string.h>


/* === freelist counters === */

#if KOS_TRACE_FREELIST

typedef struct {
  unsigned long alloc_hits;
  unsigned long alloc_misses;
  unsigned long return_hits;
  unsigned long overruns;
} kos_fl_counters_t;

#define KOS_FL_NAME_DYN_NODE     "dyn_node"
#define KOS_FL_NAME_CONTINUATION "continuation"
#define KOS_FL_NAME_CONTEXT      "context"
#define KOS_FL_NAME_STACK_BLOCK  "stack_block"
#define KOS_FL_NAME_PAIR         "pair"

static kos_fl_counters_t fl_dyn_node     = {0, 0, 0, 0};
static kos_fl_counters_t fl_continuation = {0, 0, 0, 0};
static kos_fl_counters_t fl_context      = {0, 0, 0, 0};
static kos_fl_counters_t fl_stack_block  = {0, 0, 0, 0};
static kos_fl_counters_t fl_pair         = {0, 0, 0, 0};


static kos_fl_counters_t *kos_trace_fl_lookup(const char *name) {
  if (strcmp(name, KOS_FL_NAME_DYN_NODE) == 0)
    return &fl_dyn_node;
  if (strcmp(name, KOS_FL_NAME_CONTINUATION) == 0)
    return &fl_continuation;
  if (strcmp(name, KOS_FL_NAME_CONTEXT) == 0)
    return &fl_context;
  if (strcmp(name, KOS_FL_NAME_STACK_BLOCK) == 0)
    return &fl_stack_block;
  if (strcmp(name, KOS_FL_NAME_PAIR) == 0)
    return &fl_pair;
  return NULL;
}


void kos_trace_fl_alloc(const char *name, int from_freelist) {
  kos_fl_counters_t *c = kos_trace_fl_lookup(name);
  if (c) {
    if (from_freelist)
      c->alloc_hits++;
    else
      c->alloc_misses++;
  }
}


void kos_trace_fl_return(const char *name, int overrun) {
  kos_fl_counters_t *c = kos_trace_fl_lookup(name);
  if (c) {
    if (overrun)
      c->overruns++;
    else
      c->return_hits++;
  }
}

#endif


/* === init / clear === */

void kos_trace_init(void) {
#if KOS_TRACE_FREELIST
  memset(&fl_dyn_node, 0, sizeof(fl_dyn_node));
  memset(&fl_continuation, 0, sizeof(fl_continuation));
  memset(&fl_context, 0, sizeof(fl_context));
  memset(&fl_stack_block, 0, sizeof(fl_stack_block));
  memset(&fl_pair, 0, sizeof(fl_pair));
#endif
}


void kos_trace_clear(void) {
  kos_trace_init();
}


/* === report builder === */

static int add_fl_section(PyObject *report) {
#if KOS_TRACE_FREELIST
  PyObject *freelists = PyDict_New();
  if (! freelists)
    return -1;

#define ADD_FL_ENTRY(name, counters) do {                              \
    PyObject *entry = PyDict_New();                                    \
    if (! entry) { Py_DECREF(freelists); return -1; }                   \
    if (PyDict_SetItemString(entry, "alloc_hits",                      \
            PyLong_FromUnsignedLong((counters).alloc_hits)) < 0 ||      \
        PyDict_SetItemString(entry, "alloc_misses",                    \
            PyLong_FromUnsignedLong((counters).alloc_misses)) < 0 ||    \
        PyDict_SetItemString(entry, "return_hits",                     \
            PyLong_FromUnsignedLong((counters).return_hits)) < 0 ||     \
        PyDict_SetItemString(entry, "overruns",                        \
            PyLong_FromUnsignedLong((counters).overruns)) < 0) {        \
      Py_DECREF(entry);                                                \
      Py_DECREF(freelists);                                            \
      return -1;                                                       \
    }                                                                  \
    if (PyDict_SetItemString(freelists, name, entry) < 0) {            \
      Py_DECREF(entry);                                                \
      Py_DECREF(freelists);                                            \
      return -1;                                                       \
    }                                                                  \
    Py_DECREF(entry);                                                  \
  } while (0)

  ADD_FL_ENTRY(KOS_FL_NAME_DYN_NODE, fl_dyn_node);
  ADD_FL_ENTRY(KOS_FL_NAME_CONTINUATION, fl_continuation);
  ADD_FL_ENTRY(KOS_FL_NAME_CONTEXT, fl_context);
  ADD_FL_ENTRY(KOS_FL_NAME_STACK_BLOCK, fl_stack_block);
  ADD_FL_ENTRY(KOS_FL_NAME_PAIR, fl_pair);

#undef ADD_FL_ENTRY

  if (PyDict_SetItemString(report, "freelists", freelists) < 0) {
    Py_DECREF(freelists);
    return -1;
  }
  Py_DECREF(freelists);
#endif
  return 0;
}


PyObject *kos_trace_report(void) {
  PyObject *report = PyDict_New();
  if (! report)
    return NULL;

  PyObject *flags = PyDict_New();
  if (! flags) {
    Py_DECREF(report);
    return NULL;
  }

  if (PyDict_SetItemString(flags, "freelist",
          KOS_TRACE_FREELIST ? Py_True : Py_False) < 0 ||
      PyDict_SetItemString(flags, "intern",
          KOS_TRACE_INTERN ? Py_True : Py_False) < 0 ||
      PyDict_SetItemString(flags, "events",
          KOS_TRACE_EVENTS ? Py_True : Py_False) < 0 ||
      PyDict_SetItemString(flags, "timing",
          KOS_TRACE_TIMING ? Py_True : Py_False) < 0) {
    Py_DECREF(flags);
    Py_DECREF(report);
    return NULL;
  }

  if (PyDict_SetItemString(report, "flags", flags) < 0) {
    Py_DECREF(flags);
    Py_DECREF(report);
    return NULL;
  }
  Py_DECREF(flags);

  if (add_fl_section(report) < 0) {
    Py_DECREF(report);
    return NULL;
  }

  return report;
}


// The end.
