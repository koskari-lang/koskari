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
   builder for freelist, events, and timing features.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


#include "lib.h"
#include "tracing.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/* === event counters (VM / dynchain) === */

#if KOS_TRACE_EVENTS

static uint64_t kos_te_counters[KOS_TE__COUNT];

static const char *const kos_te_names[KOS_TE__COUNT] = {
  "dyn_handle_enter",
  "dyn_unwind_step",
  "transfer_plan_build",
  "raise_local_walk_iter",
};


void kos_trace_events_inc(KosTraceEventId id) {
  if ((unsigned) id < (unsigned) KOS_TE__COUNT)
    kos_te_counters[id]++;
}

#endif


/* === per-opcode timing (eval handlers) === */

#if KOS_TRACE_TIMING

typedef struct {
  uint64_t count;
  uint64_t total_ns;
} kos_trace_op_slot_t;

static kos_trace_op_slot_t kos_op_timing[KOS_OP_THEEND + 1];


uint64_t kos_trace_now_ns(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}


void kos_trace_opcode_record(unsigned op, uint64_t delta_ns) {
  if (op > (unsigned) KOS_OP_THEEND)
    return;
  kos_op_timing[op].count++;
  kos_op_timing[op].total_ns += delta_ns;
}


static kos_trace_op_slot_t kos_post_handler_timing[KOS_TP__COUNT];

static _Thread_local uint64_t kos_tp_roll_t0;


void kos_trace_post_op_record(unsigned id, uint64_t delta_ns) {
  if (id >= (unsigned) KOS_TP__COUNT)
    return;
  kos_post_handler_timing[id].count++;
  kos_post_handler_timing[id].total_ns += delta_ns;
}


void kos_trace_post_op_enter(void) {
  kos_tp_roll_t0 = kos_trace_now_ns();
}


void kos_trace_post_op_exit(void) {
  kos_trace_post_op_record((unsigned) KOS_TP_rollup,
                           kos_trace_now_ns() - kos_tp_roll_t0);
}

#endif


/* === eval state sync (counts + monotonic wall time per API) === */

#if KOS_TRACE_EVAL_SYNC

typedef struct {
  uint64_t count;
  uint64_t total_ns;
} kos_trace_evs_slot_t;

static kos_trace_evs_slot_t kos_tevs_slots[KOS_TEVS__COUNT];

static const char *const kos_tevs_names[KOS_TEVS__COUNT] = {
  "init",
  "rebuild_live_from_chain_unused",
  "flush_live_regs",
  "apply_dyn_caches",
  "refresh_dyn_caches",
  "patch_dyn_caches",
};


uint64_t kos_trace_eval_sync_now(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}


void kos_trace_eval_sync_record(KosTraceEvalSyncId id, uint64_t delta_ns) {
  if ((unsigned) id >= (unsigned) KOS_TEVS__COUNT)
    return;
  kos_tevs_slots[id].count++;
  kos_tevs_slots[id].total_ns += delta_ns;
}

#endif


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
#if KOS_TRACE_EVENTS
  memset(kos_te_counters, 0, sizeof(kos_te_counters));
#endif
#if KOS_TRACE_TIMING
  memset(kos_op_timing, 0, sizeof(kos_op_timing));
  memset(kos_post_handler_timing, 0, sizeof(kos_post_handler_timing));
#endif
#if KOS_TRACE_EVAL_SYNC
  memset(kos_tevs_slots, 0, sizeof(kos_tevs_slots));
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


#if KOS_TRACE_TIMING

#define KOS_TRACE_OPCODE_TOP_N 10

typedef struct {
  unsigned idx;
  uint64_t count;
  uint64_t total_ns;
  uint64_t avg_ns;
} kos_trace_op_aggregate_t;


static int cmp_op_agg_total_ns_desc(const void *a, const void *b) {
  const kos_trace_op_aggregate_t *x = a;
  const kos_trace_op_aggregate_t *y = b;

  if (x->total_ns < y->total_ns)
    return 1;
  if (x->total_ns > y->total_ns)
    return -1;
  return 0;
}


static int cmp_op_agg_avg_ns_desc(const void *a, const void *b) {
  const kos_trace_op_aggregate_t *x = a;
  const kos_trace_op_aggregate_t *y = b;

  if (x->avg_ns < y->avg_ns)
    return 1;
  if (x->avg_ns > y->avg_ns)
    return -1;
  return 0;
}

#endif


static int add_events_section(PyObject *report) {
#if KOS_TRACE_EVENTS
  PyObject *events = PyDict_New();
  if (! events)
    return -1;

  Py_ssize_t i;
  for (i = 0; i < (Py_ssize_t) KOS_TE__COUNT; i++) {
    PyObject *n = PyLong_FromUnsignedLongLong(kos_te_counters[i]);
    if (! n) {
      Py_DECREF(events);
      return -1;
    }
    if (PyDict_SetItemString(events, kos_te_names[i], n) < 0) {
      Py_DECREF(n);
      Py_DECREF(events);
      return -1;
    }
    Py_DECREF(n);
  }

  if (PyDict_SetItemString(report, "events", events) < 0) {
    Py_DECREF(events);
    return -1;
  }
  Py_DECREF(events);
#endif
  return 0;
}


static int add_eval_state_sync_section(PyObject *report) {
#if KOS_TRACE_EVAL_SYNC
  PyObject *sec = PyDict_New();
  Py_ssize_t i;

  if (! sec)
    return -1;

  for (i = 0; i < (Py_ssize_t) KOS_TEVS__COUNT; i++) {
    PyObject *entry = PyDict_New();
    if (! entry) {
      Py_DECREF(sec);
      return -1;
    }
    if (PyDict_SetItemString(entry, "count",
            PyLong_FromUnsignedLongLong(kos_tevs_slots[i].count)) < 0 ||
        PyDict_SetItemString(entry, "total_ns",
            PyLong_FromUnsignedLongLong(kos_tevs_slots[i].total_ns)) < 0) {
      Py_DECREF(entry);
      Py_DECREF(sec);
      return -1;
    }
    if (PyDict_SetItemString(sec, kos_tevs_names[i], entry) < 0) {
      Py_DECREF(entry);
      Py_DECREF(sec);
      return -1;
    }
    Py_DECREF(entry);
  }

  if (PyDict_SetItemString(report, "eval_state_sync", sec) < 0) {
    Py_DECREF(sec);
    return -1;
  }
  Py_DECREF(sec);
#endif
  return 0;
}


static int add_opcode_timing_section(PyObject *report) {
#if KOS_TRACE_TIMING
  PyObject *ot = NULL;
  PyObject *summary = NULL;
  kos_trace_op_aggregate_t aggs[KOS_OP_THEEND + 1];
  kos_trace_op_aggregate_t tmp[KOS_OP_THEEND + 1];
  unsigned i;
  size_t n_agg;
  uint64_t total_executions;
  uint64_t total_ns_all;
  uint64_t avg_ns_per_dispatch;
  unsigned opcode_count_nonzero;
  const char *max_total_ns_op;
  uint64_t max_total_ns;
  const char *max_avg_ns_op;
  uint64_t max_avg_ns;
  int rc = -1;

  ot = PyDict_New();
  if (! ot)
    return -1;

  total_executions = 0;
  total_ns_all = 0;
  opcode_count_nonzero = 0;
  max_total_ns_op = "";
  max_total_ns = 0;
  max_avg_ns_op = "";
  max_avg_ns = 0;
  n_agg = 0;

  for (i = 0; i <= (unsigned) KOS_OP_THEEND; i++) {
    uint64_t c;
    uint64_t t;
    uint64_t avg;

    c = kos_op_timing[i].count;
    t = kos_op_timing[i].total_ns;
    if (c == 0)
      continue;

    total_executions += c;
    total_ns_all += t;
    opcode_count_nonzero++;

    if (t > max_total_ns) {
      max_total_ns = t;
      max_total_ns_op = kos_opcode_name((KosOpCode) i);
    }

    avg = t / c;
    if (avg > max_avg_ns) {
      max_avg_ns = avg;
      max_avg_ns_op = kos_opcode_name((KosOpCode) i);
    }

    aggs[n_agg].idx = i;
    aggs[n_agg].count = c;
    aggs[n_agg].total_ns = t;
    aggs[n_agg].avg_ns = avg;
    n_agg++;
  }

  if (total_executions > 0)
    avg_ns_per_dispatch = total_ns_all / total_executions;
  else
    avg_ns_per_dispatch = 0;

  for (i = 0; i <= (unsigned) KOS_OP_THEEND; i++) {
    PyObject *entry;
    const char *name;
    uint64_t c;
    uint64_t t;
    uint64_t avg;
    double share_time;
    double share_exec;
    double ratio_mean;

    c = kos_op_timing[i].count;
    t = kos_op_timing[i].total_ns;
    if (c == 0)
      continue;

    avg = t / c;
    if (total_ns_all > 0)
      share_time = (double) t / (double) total_ns_all;
    else
      share_time = 0.0;
    if (total_executions > 0)
      share_exec = (double) c / (double) total_executions;
    else
      share_exec = 0.0;
    if (avg_ns_per_dispatch > 0)
      ratio_mean = (double) avg / (double) avg_ns_per_dispatch;
    else
      ratio_mean = 0.0;

    entry = PyDict_New();
    if (! entry)
      goto fail;

    if (PyDict_SetItemString(entry, "count",
            PyLong_FromUnsignedLongLong(c)) < 0 ||
        PyDict_SetItemString(entry, "total_ns",
            PyLong_FromUnsignedLongLong(t)) < 0 ||
        PyDict_SetItemString(entry, "avg_ns",
            PyLong_FromUnsignedLongLong(avg)) < 0 ||
        PyDict_SetItemString(entry, "share_of_time",
            PyFloat_FromDouble(share_time)) < 0 ||
        PyDict_SetItemString(entry, "share_of_executions",
            PyFloat_FromDouble(share_exec)) < 0 ||
        PyDict_SetItemString(entry, "ratio_vs_mean",
            PyFloat_FromDouble(ratio_mean)) < 0) {
      Py_DECREF(entry);
      goto fail;
    }
    name = kos_opcode_name((KosOpCode) i);
    if (PyDict_SetItemString(ot, name, entry) < 0) {
      Py_DECREF(entry);
      goto fail;
    }
    Py_DECREF(entry);
  }

  summary = PyDict_New();
  if (! summary)
    goto fail;

  if (PyDict_SetItemString(summary, "total_executions",
          PyLong_FromUnsignedLongLong(total_executions)) < 0 ||
      PyDict_SetItemString(summary, "total_ns",
          PyLong_FromUnsignedLongLong(total_ns_all)) < 0 ||
      PyDict_SetItemString(summary, "avg_ns_per_dispatch",
          PyLong_FromUnsignedLongLong(avg_ns_per_dispatch)) < 0 ||
      PyDict_SetItemString(summary, "opcode_count_nonzero",
          PyLong_FromUnsignedLong((unsigned long) opcode_count_nonzero)) < 0 ||
      PyDict_SetItemString(summary, "max_total_ns_op",
          PyUnicode_FromString(max_total_ns_op)) < 0 ||
      PyDict_SetItemString(summary, "max_total_ns",
          PyLong_FromUnsignedLongLong(max_total_ns)) < 0 ||
      PyDict_SetItemString(summary, "max_avg_ns_op",
          PyUnicode_FromString(max_avg_ns_op)) < 0 ||
      PyDict_SetItemString(summary, "max_avg_ns",
          PyLong_FromUnsignedLongLong(max_avg_ns)) < 0) {
    goto fail;
  }

  {
    PyObject *top_total;
    PyObject *top_avg;
    size_t k;
    size_t lim;

    top_total = PyList_New(0);
    top_avg = PyList_New(0);
    if (! top_total || ! top_avg) {
      Py_XDECREF(top_total);
      Py_XDECREF(top_avg);
      goto fail;
    }

    if (n_agg > 0) {
      memcpy(tmp, aggs, n_agg * sizeof(kos_trace_op_aggregate_t));
      qsort(tmp, n_agg, sizeof(kos_trace_op_aggregate_t),
            cmp_op_agg_total_ns_desc);
      lim = n_agg;
      if (lim > (size_t) KOS_TRACE_OPCODE_TOP_N)
        lim = (size_t) KOS_TRACE_OPCODE_TOP_N;
      for (k = 0; k < lim; k++) {
        PyObject *row;
        const char *opname;

        row = PyDict_New();
        if (! row) {
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        opname = kos_opcode_name((KosOpCode) tmp[k].idx);
        if (PyDict_SetItemString(row, "op",
                PyUnicode_FromString(opname)) < 0 ||
            PyDict_SetItemString(row, "total_ns",
                PyLong_FromUnsignedLongLong(tmp[k].total_ns)) < 0) {
          Py_DECREF(row);
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        if (PyList_Append(top_total, row) < 0) {
          Py_DECREF(row);
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        Py_DECREF(row);
      }

      memcpy(tmp, aggs, n_agg * sizeof(kos_trace_op_aggregate_t));
      qsort(tmp, n_agg, sizeof(kos_trace_op_aggregate_t),
            cmp_op_agg_avg_ns_desc);
      lim = n_agg;
      if (lim > (size_t) KOS_TRACE_OPCODE_TOP_N)
        lim = (size_t) KOS_TRACE_OPCODE_TOP_N;
      for (k = 0; k < lim; k++) {
        PyObject *row;
        const char *opname;

        row = PyDict_New();
        if (! row) {
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        opname = kos_opcode_name((KosOpCode) tmp[k].idx);
        if (PyDict_SetItemString(row, "op",
                PyUnicode_FromString(opname)) < 0 ||
            PyDict_SetItemString(row, "avg_ns",
                PyLong_FromUnsignedLongLong(tmp[k].avg_ns)) < 0) {
          Py_DECREF(row);
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        if (PyList_Append(top_avg, row) < 0) {
          Py_DECREF(row);
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        Py_DECREF(row);
      }
    }

    if (PyDict_SetItemString(summary, "top_by_total_ns", top_total) < 0 ||
        PyDict_SetItemString(summary, "top_by_avg_ns", top_avg) < 0) {
      Py_DECREF(top_total);
      Py_DECREF(top_avg);
      goto fail;
    }
    Py_DECREF(top_total);
    Py_DECREF(top_avg);
  }

  if (PyDict_SetItemString(report, "opcode_timing", ot) < 0)
    goto fail;
  Py_DECREF(ot);
  ot = NULL;

  if (PyDict_SetItemString(report, "opcode_timing_summary", summary) < 0)
    goto fail;
  Py_DECREF(summary);
  summary = NULL;

  rc = 0;
  return rc;

fail:
  Py_XDECREF(ot);
  Py_XDECREF(summary);
  return -1;
#else
  (void) report;
#endif
  return 0;
}


#if KOS_TRACE_TIMING

#define KOS_TRACE_POST_HANDLER_TOP_N 10

static const char *const kos_tp_names[KOS_TP__COUNT] = {
  "rollup",
  "parent_return",
  "control_returned",
  "event_raise",
  "event_await",
  "event_yield",
  "return_boundary",
};


static int cmp_post_agg_total_ns_desc(const void *a, const void *b) {
  const kos_trace_op_aggregate_t *x = a;
  const kos_trace_op_aggregate_t *y = b;

  if (x->total_ns < y->total_ns)
    return 1;
  if (x->total_ns > y->total_ns)
    return -1;
  return 0;
}


static int cmp_post_agg_avg_ns_desc(const void *a, const void *b) {
  const kos_trace_op_aggregate_t *x = a;
  const kos_trace_op_aggregate_t *y = b;

  if (x->avg_ns < y->avg_ns)
    return 1;
  if (x->avg_ns > y->avg_ns)
    return -1;
  return 0;
}


static int add_post_handler_timing_section(PyObject *report) {
  PyObject *ph = NULL;
  PyObject *summary = NULL;
  kos_trace_op_aggregate_t aggs[KOS_TP__COUNT];
  kos_trace_op_aggregate_t tmp[KOS_TP__COUNT];
  unsigned i;
  size_t n_agg;
  uint64_t total_executions;
  uint64_t total_ns_all;
  uint64_t avg_ns_per_dispatch;
  unsigned handler_count_nonzero;
  const char *max_total_ns_handler;
  uint64_t max_total_ns;
  const char *max_avg_ns_handler;
  uint64_t max_avg_ns;
  uint64_t rollup_count;
  uint64_t rollup_total_ns;
  uint64_t rollup_avg_ns;
  int rc = -1;

  ph = PyDict_New();
  if (! ph)
    return -1;

  total_executions = 0;
  total_ns_all = 0;
  handler_count_nonzero = 0;
  max_total_ns_handler = "";
  max_total_ns = 0;
  max_avg_ns_handler = "";
  max_avg_ns = 0;
  n_agg = 0;

  for (i = 0; i < (unsigned) KOS_TP__COUNT; i++) {
    uint64_t c;
    uint64_t t;
    uint64_t avg;

    c = kos_post_handler_timing[i].count;
    t = kos_post_handler_timing[i].total_ns;
    if (c == 0)
      continue;

    total_executions += c;
    total_ns_all += t;
    handler_count_nonzero++;

    if (t > max_total_ns) {
      max_total_ns = t;
      max_total_ns_handler = kos_tp_names[i];
    }

    avg = t / c;
    if (avg > max_avg_ns) {
      max_avg_ns = avg;
      max_avg_ns_handler = kos_tp_names[i];
    }

    aggs[n_agg].idx = i;
    aggs[n_agg].count = c;
    aggs[n_agg].total_ns = t;
    aggs[n_agg].avg_ns = avg;
    n_agg++;
  }

  if (total_executions > 0)
    avg_ns_per_dispatch = total_ns_all / total_executions;
  else
    avg_ns_per_dispatch = 0;

  rollup_count = kos_post_handler_timing[KOS_TP_rollup].count;
  rollup_total_ns = kos_post_handler_timing[KOS_TP_rollup].total_ns;
  if (rollup_count > 0)
    rollup_avg_ns = rollup_total_ns / rollup_count;
  else
    rollup_avg_ns = 0;

  for (i = 0; i < (unsigned) KOS_TP__COUNT; i++) {
    PyObject *entry;
    const char *name;
    uint64_t c;
    uint64_t t;
    uint64_t avg;
    double share_time;
    double share_exec;
    double ratio_mean;

    c = kos_post_handler_timing[i].count;
    t = kos_post_handler_timing[i].total_ns;
    if (c == 0)
      continue;

    avg = t / c;
    if (total_ns_all > 0)
      share_time = (double) t / (double) total_ns_all;
    else
      share_time = 0.0;
    if (total_executions > 0)
      share_exec = (double) c / (double) total_executions;
    else
      share_exec = 0.0;
    if (avg_ns_per_dispatch > 0)
      ratio_mean = (double) avg / (double) avg_ns_per_dispatch;
    else
      ratio_mean = 0.0;

    entry = PyDict_New();
    if (! entry)
      goto fail;

    if (PyDict_SetItemString(entry, "count",
            PyLong_FromUnsignedLongLong(c)) < 0 ||
        PyDict_SetItemString(entry, "total_ns",
            PyLong_FromUnsignedLongLong(t)) < 0 ||
        PyDict_SetItemString(entry, "avg_ns",
            PyLong_FromUnsignedLongLong(avg)) < 0 ||
        PyDict_SetItemString(entry, "share_of_time",
            PyFloat_FromDouble(share_time)) < 0 ||
        PyDict_SetItemString(entry, "share_of_executions",
            PyFloat_FromDouble(share_exec)) < 0 ||
        PyDict_SetItemString(entry, "ratio_vs_mean",
            PyFloat_FromDouble(ratio_mean)) < 0) {
      Py_DECREF(entry);
      goto fail;
    }
    name = kos_tp_names[i];
    if (PyDict_SetItemString(ph, name, entry) < 0) {
      Py_DECREF(entry);
      goto fail;
    }
    Py_DECREF(entry);
  }

  summary = PyDict_New();
  if (! summary)
    goto fail;

  if (PyDict_SetItemString(summary, "total_executions",
          PyLong_FromUnsignedLongLong(total_executions)) < 0 ||
      PyDict_SetItemString(summary, "total_ns",
          PyLong_FromUnsignedLongLong(total_ns_all)) < 0 ||
      PyDict_SetItemString(summary, "avg_ns_per_dispatch",
          PyLong_FromUnsignedLongLong(avg_ns_per_dispatch)) < 0 ||
      PyDict_SetItemString(summary, "handler_count_nonzero",
          PyLong_FromUnsignedLong((unsigned long) handler_count_nonzero)) < 0 ||
      PyDict_SetItemString(summary, "max_total_ns_handler",
          PyUnicode_FromString(max_total_ns_handler)) < 0 ||
      PyDict_SetItemString(summary, "max_total_ns",
          PyLong_FromUnsignedLongLong(max_total_ns)) < 0 ||
      PyDict_SetItemString(summary, "max_avg_ns_handler",
          PyUnicode_FromString(max_avg_ns_handler)) < 0 ||
      PyDict_SetItemString(summary, "max_avg_ns",
          PyLong_FromUnsignedLongLong(max_avg_ns)) < 0 ||
      PyDict_SetItemString(summary, "rollup_invocations",
          PyLong_FromUnsignedLongLong(rollup_count)) < 0 ||
      PyDict_SetItemString(summary, "rollup_total_ns",
          PyLong_FromUnsignedLongLong(rollup_total_ns)) < 0 ||
      PyDict_SetItemString(summary, "rollup_avg_ns",
          PyLong_FromUnsignedLongLong(rollup_avg_ns)) < 0) {
    goto fail;
  }

  {
    PyObject *top_total;
    PyObject *top_avg;
    size_t k;
    size_t lim;

    top_total = PyList_New(0);
    top_avg = PyList_New(0);
    if (! top_total || ! top_avg) {
      Py_XDECREF(top_total);
      Py_XDECREF(top_avg);
      goto fail;
    }

    if (n_agg > 0) {
      memcpy(tmp, aggs, n_agg * sizeof(kos_trace_op_aggregate_t));
      qsort(tmp, n_agg, sizeof(kos_trace_op_aggregate_t),
            cmp_post_agg_total_ns_desc);
      lim = n_agg;
      if (lim > (size_t) KOS_TRACE_POST_HANDLER_TOP_N)
        lim = (size_t) KOS_TRACE_POST_HANDLER_TOP_N;
      for (k = 0; k < lim; k++) {
        PyObject *row;
        const char *hname;

        row = PyDict_New();
        if (! row) {
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        hname = kos_tp_names[tmp[k].idx];
        if (PyDict_SetItemString(row, "handler",
                PyUnicode_FromString(hname)) < 0 ||
            PyDict_SetItemString(row, "total_ns",
                PyLong_FromUnsignedLongLong(tmp[k].total_ns)) < 0) {
          Py_DECREF(row);
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        if (PyList_Append(top_total, row) < 0) {
          Py_DECREF(row);
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        Py_DECREF(row);
      }

      memcpy(tmp, aggs, n_agg * sizeof(kos_trace_op_aggregate_t));
      qsort(tmp, n_agg, sizeof(kos_trace_op_aggregate_t),
            cmp_post_agg_avg_ns_desc);
      lim = n_agg;
      if (lim > (size_t) KOS_TRACE_POST_HANDLER_TOP_N)
        lim = (size_t) KOS_TRACE_POST_HANDLER_TOP_N;
      for (k = 0; k < lim; k++) {
        PyObject *row;
        const char *hname;

        row = PyDict_New();
        if (! row) {
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        hname = kos_tp_names[tmp[k].idx];
        if (PyDict_SetItemString(row, "handler",
                PyUnicode_FromString(hname)) < 0 ||
            PyDict_SetItemString(row, "avg_ns",
                PyLong_FromUnsignedLongLong(tmp[k].avg_ns)) < 0) {
          Py_DECREF(row);
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        if (PyList_Append(top_avg, row) < 0) {
          Py_DECREF(row);
          Py_DECREF(top_total);
          Py_DECREF(top_avg);
          goto fail;
        }
        Py_DECREF(row);
      }
    }

    if (PyDict_SetItemString(summary, "top_by_total_ns", top_total) < 0 ||
        PyDict_SetItemString(summary, "top_by_avg_ns", top_avg) < 0) {
      Py_DECREF(top_total);
      Py_DECREF(top_avg);
      goto fail;
    }
    Py_DECREF(top_total);
    Py_DECREF(top_avg);
  }

  if (PyDict_SetItemString(report, "post_handler_timing", ph) < 0)
    goto fail;
  Py_DECREF(ph);
  ph = NULL;

  if (PyDict_SetItemString(report, "post_handler_timing_summary", summary) < 0)
    goto fail;
  Py_DECREF(summary);
  summary = NULL;

  rc = 0;
  return rc;

fail:
  Py_XDECREF(ph);
  Py_XDECREF(summary);
  return -1;
}

#endif


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
      PyDict_SetItemString(flags, "events",
          KOS_TRACE_EVENTS ? Py_True : Py_False) < 0 ||
      PyDict_SetItemString(flags, "timing",
          KOS_TRACE_TIMING ? Py_True : Py_False) < 0 ||
      PyDict_SetItemString(flags, "eval_state_sync",
          KOS_TRACE_EVAL_SYNC ? Py_True : Py_False) < 0) {
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

  if (add_events_section(report) < 0) {
    Py_DECREF(report);
    return NULL;
  }

  if (add_eval_state_sync_section(report) < 0) {
    Py_DECREF(report);
    return NULL;
  }

  if (add_opcode_timing_section(report) < 0) {
    Py_DECREF(report);
    return NULL;
  }

#if KOS_TRACE_TIMING
  if (add_post_handler_timing_section(report) < 0) {
    Py_DECREF(report);
    return NULL;
  }
#endif

  return report;
}


// The end.
