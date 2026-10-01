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
  "transfer_goal_new",
  "raise_local_walk_iter",
  "seg_inline_install",
  "seg_inline_spill",
  "retarget_walk_fallback",
  "parent_slot_walk",
  "live_rebuild",
  "call_frame_cached",
  "call_simple_steal",
  "call_simple_vector",
  "call_tail_inplace",
};


void kos_trace_events_inc(KosTraceEventId id) {
  if ((unsigned) id < (unsigned) KOS_TE__COUNT)
    kos_te_counters[id]++;
}

#endif


#if KOS_TRACE_OPCACHE

static uint64_t kos_oc_counters[KOS_OC__COUNT];

static const char *const kos_oc_names[KOS_OC__COUNT] = {
  "method_hit",
  "method_miss",
  "method_bypass",
  "method_uncached",
  "field_hit",
  "field_generic",
  "field_miss",
  "field_fallback",
  "elector_hit",
  "elector_miss",
  "elector_bypass",
  "elector_uncached",
};


void kos_trace_opcache_inc(KosTraceOpCacheId id) {
  if ((unsigned) id < (unsigned) KOS_OC__COUNT)
    kos_oc_counters[id]++;
}

#endif


/* === per-opcode timing (eval handlers) === */

#if KOS_TRACE_TIMING

/* Totals are in clock ticks; the report converts them to nanoseconds
   with the calibration taken between init and report time. */
typedef struct {
  uint64_t count;
  uint64_t total_ticks;
} kos_trace_op_slot_t;

static kos_trace_op_slot_t kos_op_timing[KOS_TRACE_SLOT_COUNT];

/* calibration: wall time and ticks read together at init, and the
   cost of one step (two clock reads and the bookkeeping) measured
   there, so a report reader can subtract it from an opcode's average */
static uint64_t kos_trace_cal_ns0;
static uint64_t kos_trace_cal_ticks0;
static uint64_t kos_trace_step_overhead_ticks;

/* the one open slot per thread, and the slots suspended by nested
   evaluator entries */
#define KOS_TRACE_SLOT_NONE   ((unsigned) -1)
#define KOS_TRACE_NEST_MAX    64

static _Thread_local unsigned kos_trace_open = KOS_TRACE_SLOT_NONE;
static _Thread_local uint64_t kos_trace_open_t0;
static _Thread_local unsigned kos_trace_nest[KOS_TRACE_NEST_MAX];
static _Thread_local unsigned kos_trace_depth;


uint64_t kos_trace_now_ns(void) {
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return 0;
  return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}


/**
   The timing clock: the cycle counter where the compiler exposes one
   (clang on every target, gcc on x86-64), else the monotonic clock in
   nanoseconds, in which case the calibration ratio is 1.
*/
uint64_t kos_trace_ticks(void) {
#if defined(__clang__)
  return (uint64_t) __builtin_readcyclecounter();
#elif defined(__x86_64__) && defined(__GNUC__)
  return (uint64_t) __builtin_ia32_rdtsc();
#else
  return kos_trace_now_ns();
#endif
}


static const char *kos_trace_clock_name(void) {
#if defined(__clang__) || (defined(__x86_64__) && defined(__GNUC__))
  return "cycle-counter";
#else
  return "clock_gettime";
#endif
}


/**
   Nanoseconds per tick, from the wall time and ticks elapsed since
   init; 1.0 when too little has elapsed to measure.
*/
static double kos_trace_ns_per_tick(void) {
  uint64_t ns = kos_trace_now_ns() - kos_trace_cal_ns0;
  uint64_t ticks = kos_trace_ticks() - kos_trace_cal_ticks0;

  if (ticks == 0 || ns == 0)
    return 1.0;
  return (double) ns / (double) ticks;
}


static inline void kos_trace_close_open(uint64_t now) {
  if (kos_trace_open != KOS_TRACE_SLOT_NONE)
    kos_op_timing[kos_trace_open].total_ticks += now - kos_trace_open_t0;
}


/**
   Close the open slot and open slot: the transfer between two handlers,
   from a handler to the trampoline, or into a resume. A slot's count
   is the number of times it was opened.
*/
void kos_trace_step(unsigned slot) {
  uint64_t now = kos_trace_ticks();

  kos_trace_close_open(now);
  if (slot >= KOS_TRACE_SLOT_COUNT)
    slot = KOS_TRACE_SLOT_NONE;
  else
    kos_op_timing[slot].count++;
  kos_trace_open = slot;
  kos_trace_open_t0 = now;
}


void kos_trace_exit(void) {
  kos_trace_close_open(kos_trace_ticks());
  kos_trace_open = KOS_TRACE_SLOT_NONE;
}


/**
   A nested evaluator starts: suspend the open slot (its time so far is
   banked, its count already taken) so the nested run charges its own
   slots. Beyond the nesting limit the outer slot is simply closed.
*/
void kos_trace_eval_push(void) {
  uint64_t now = kos_trace_ticks();

  kos_trace_close_open(now);
  if (kos_trace_depth < KOS_TRACE_NEST_MAX)
    kos_trace_nest[kos_trace_depth] = kos_trace_open;
  kos_trace_depth++;
  kos_trace_open = KOS_TRACE_SLOT_NONE;
}


/**
   A nested evaluator ends: resume the slot it suspended, timing from
   now, so the outer opcode's remaining work lands on it.
*/
void kos_trace_eval_pop(void) {
  uint64_t now = kos_trace_ticks();

  kos_trace_close_open(now);
  kos_trace_open = KOS_TRACE_SLOT_NONE;
  if (kos_trace_depth == 0)
    return;
  kos_trace_depth--;
  if (kos_trace_depth < KOS_TRACE_NEST_MAX)
    kos_trace_open = kos_trace_nest[kos_trace_depth];
  kos_trace_open_t0 = now;
}


/**
   Measure the cost of one step: open and close the trampoline slot a
   few thousand times and take the mean, then undo the counts.
*/
static void kos_trace_measure_step_overhead(void) {
  const unsigned n = 4096;
  uint64_t t0;
  uint64_t t1;
  unsigned i;

  kos_trace_exit();
  t0 = kos_trace_ticks();
  for (i = 0; i < n; i++)
    kos_trace_step(KOS_TRACE_SLOT_TRAMPOLINE);
  kos_trace_exit();
  t1 = kos_trace_ticks();
  kos_trace_step_overhead_ticks = (t1 - t0) / n;
  kos_op_timing[KOS_TRACE_SLOT_TRAMPOLINE].count = 0;
  kos_op_timing[KOS_TRACE_SLOT_TRAMPOLINE].total_ticks = 0;
}


static const char *kos_trace_slot_name(unsigned slot) {
  if (slot <= (unsigned) KOS_OP_THEEND)
    return kos_opcode_name((KosOpCode) slot);
  if (slot == KOS_TRACE_SLOT_RESUME)
    return "resume";
  if (slot == KOS_TRACE_SLOT_TRAMPOLINE)
    return "trampoline";
  return "UNKNOWN_SLOT";
}


static kos_trace_op_slot_t kos_post_handler_timing[KOS_TP__COUNT];

static _Thread_local uint64_t kos_tp_roll_t0;


void kos_trace_post_op_record(unsigned id, uint64_t delta) {
  if (id >= (unsigned) KOS_TP__COUNT)
    return;
  kos_post_handler_timing[id].count++;
  kos_post_handler_timing[id].total_ticks += delta;
}


void kos_trace_post_op_enter(void) {
  kos_tp_roll_t0 = kos_trace_ticks();
}


void kos_trace_post_op_exit(void) {
  kos_trace_post_op_record((unsigned) KOS_TP_rollup,
                           kos_trace_ticks() - kos_tp_roll_t0);
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
  "unused_sync_from_context",
  "flush_live_regs",
  "unused_apply_dyn_caches",
  "rebuild_live_from_chain",
  "retarget_after_push",
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
#define KOS_FL_NAME_NAMESPACE    "namespace"

static kos_fl_counters_t fl_dyn_node     = {0, 0, 0, 0};
static kos_fl_counters_t fl_continuation = {0, 0, 0, 0};
static kos_fl_counters_t fl_context      = {0, 0, 0, 0};
static kos_fl_counters_t fl_stack_block  = {0, 0, 0, 0};
static kos_fl_counters_t fl_pair         = {0, 0, 0, 0};
static kos_fl_counters_t fl_namespace    = {0, 0, 0, 0};


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
  if (strcmp(name, KOS_FL_NAME_NAMESPACE) == 0)
    return &fl_namespace;
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
  memset(&fl_namespace, 0, sizeof(fl_namespace));
#endif
#if KOS_TRACE_EVENTS
  memset(kos_te_counters, 0, sizeof(kos_te_counters));
#endif
#if KOS_TRACE_TIMING
  memset(kos_op_timing, 0, sizeof(kos_op_timing));
  kos_trace_cal_ns0 = kos_trace_now_ns();
  kos_trace_cal_ticks0 = kos_trace_ticks();
  kos_trace_measure_step_overhead();
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


#if KOS_TRACE_FREELIST || KOS_TRACE_TIMING || KOS_TRACE_EVAL_SYNC
/**
   Store value under key in dict, taking ownership of value. Returns -1
   when value is NULL (a constructor failed, exception set) or when the
   store fails; value is released on every path. Only the sections built
   under the trace flags above store fresh values this way.
*/
static int trace_set_item(PyObject *dict, const char *key, PyObject *value) {

  if (! value)
    return -1;
  if (PyDict_SetItemString(dict, key, value) < 0) {
    Py_DECREF(value);
    return -1;
  }
  Py_DECREF(value);
  return 0;
}
#endif


static int add_fl_section(PyObject *report) {
#if KOS_TRACE_FREELIST
  PyObject *freelists = PyDict_New();
  if (! freelists)
    return -1;

#define ADD_FL_ENTRY(name, counters) do {                              \
    PyObject *entry = PyDict_New();                                    \
    if (! entry) { Py_DECREF(freelists); return -1; }                   \
    if (trace_set_item(entry, "alloc_hits",                      \
            PyLong_FromUnsignedLong((counters).alloc_hits)) < 0 ||      \
        trace_set_item(entry, "alloc_misses",                    \
            PyLong_FromUnsignedLong((counters).alloc_misses)) < 0 ||    \
        trace_set_item(entry, "return_hits",                     \
            PyLong_FromUnsignedLong((counters).return_hits)) < 0 ||     \
        trace_set_item(entry, "overruns",                        \
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
  ADD_FL_ENTRY(KOS_FL_NAME_NAMESPACE, fl_namespace);

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


static int add_opcache_section(PyObject *report) {
#if KOS_TRACE_OPCACHE
  PyObject *opcache = PyDict_New();
  if (! opcache)
    return -1;

  Py_ssize_t i;
  for (i = 0; i < (Py_ssize_t) KOS_OC__COUNT; i++) {
    PyObject *n = PyLong_FromUnsignedLongLong(kos_oc_counters[i]);
    if (! n) {
      Py_DECREF(opcache);
      return -1;
    }
    if (PyDict_SetItemString(opcache, kos_oc_names[i], n) < 0) {
      Py_DECREF(n);
      Py_DECREF(opcache);
      return -1;
    }
    Py_DECREF(n);
  }

  if (PyDict_SetItemString(report, "opcache", opcache) < 0) {
    Py_DECREF(opcache);
    return -1;
  }
  Py_DECREF(opcache);
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
    if (trace_set_item(entry, "count",
            PyLong_FromUnsignedLongLong(kos_tevs_slots[i].count)) < 0 ||
        trace_set_item(entry, "total_ns",
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
  kos_trace_op_aggregate_t aggs[KOS_TRACE_SLOT_COUNT];
  kos_trace_op_aggregate_t tmp[KOS_TRACE_SLOT_COUNT];
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
  const double ns_per_tick = kos_trace_ns_per_tick();

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

  for (i = 0; i < KOS_TRACE_SLOT_COUNT; i++) {
    uint64_t c;
    uint64_t t;
    uint64_t avg;

    c = kos_op_timing[i].count;
    t = (uint64_t) ((double) kos_op_timing[i].total_ticks * ns_per_tick);
    if (c == 0)
      continue;

    total_executions += c;
    total_ns_all += t;
    opcode_count_nonzero++;

    if (t > max_total_ns) {
      max_total_ns = t;
      max_total_ns_op = kos_trace_slot_name(i);
    }

    avg = t / c;
    if (avg > max_avg_ns) {
      max_avg_ns = avg;
      max_avg_ns_op = kos_trace_slot_name(i);
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

  for (i = 0; i < KOS_TRACE_SLOT_COUNT; i++) {
    PyObject *entry;
    const char *name;
    uint64_t c;
    uint64_t t;
    uint64_t avg;
    double share_time;
    double share_exec;
    double ratio_mean;

    c = kos_op_timing[i].count;
    t = (uint64_t) ((double) kos_op_timing[i].total_ticks * ns_per_tick);
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

    if (trace_set_item(entry, "count",
            PyLong_FromUnsignedLongLong(c)) < 0 ||
        trace_set_item(entry, "total_ns",
            PyLong_FromUnsignedLongLong(t)) < 0 ||
        trace_set_item(entry, "avg_ns",
            PyLong_FromUnsignedLongLong(avg)) < 0 ||
        trace_set_item(entry, "share_of_time",
            PyFloat_FromDouble(share_time)) < 0 ||
        trace_set_item(entry, "share_of_executions",
            PyFloat_FromDouble(share_exec)) < 0 ||
        trace_set_item(entry, "ratio_vs_mean",
            PyFloat_FromDouble(ratio_mean)) < 0) {
      Py_DECREF(entry);
      goto fail;
    }
    name = kos_trace_slot_name(i);
    if (PyDict_SetItemString(ot, name, entry) < 0) {
      Py_DECREF(entry);
      goto fail;
    }
    Py_DECREF(entry);
  }

  summary = PyDict_New();
  if (! summary)
    goto fail;

  if (trace_set_item(summary, "clock",
          PyUnicode_FromString(kos_trace_clock_name())) < 0 ||
      trace_set_item(summary, "ns_per_tick",
          PyFloat_FromDouble(ns_per_tick)) < 0 ||
      trace_set_item(summary, "step_overhead_ns",
          PyFloat_FromDouble((double) kos_trace_step_overhead_ticks
                             * ns_per_tick)) < 0 ||
      trace_set_item(summary, "total_executions",
          PyLong_FromUnsignedLongLong(total_executions)) < 0 ||
      trace_set_item(summary, "total_ns",
          PyLong_FromUnsignedLongLong(total_ns_all)) < 0 ||
      trace_set_item(summary, "avg_ns_per_dispatch",
          PyLong_FromUnsignedLongLong(avg_ns_per_dispatch)) < 0 ||
      trace_set_item(summary, "opcode_count_nonzero",
          PyLong_FromUnsignedLong((unsigned long) opcode_count_nonzero)) < 0 ||
      trace_set_item(summary, "max_total_ns_op",
          PyUnicode_FromString(max_total_ns_op)) < 0 ||
      trace_set_item(summary, "max_total_ns",
          PyLong_FromUnsignedLongLong(max_total_ns)) < 0 ||
      trace_set_item(summary, "max_avg_ns_op",
          PyUnicode_FromString(max_avg_ns_op)) < 0 ||
      trace_set_item(summary, "max_avg_ns",
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
        opname = kos_trace_slot_name(tmp[k].idx);
        if (trace_set_item(row, "op",
                PyUnicode_FromString(opname)) < 0 ||
            trace_set_item(row, "total_ns",
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
        opname = kos_trace_slot_name(tmp[k].idx);
        if (trace_set_item(row, "op",
                PyUnicode_FromString(opname)) < 0 ||
            trace_set_item(row, "avg_ns",
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
  "return_event",
  "plain_return",
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
  const double ns_per_tick = kos_trace_ns_per_tick();

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
    t = (uint64_t) ((double) kos_post_handler_timing[i].total_ticks * ns_per_tick);
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
  rollup_total_ns = (uint64_t) ((double)
      kos_post_handler_timing[KOS_TP_rollup].total_ticks * ns_per_tick);
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
    t = (uint64_t) ((double) kos_post_handler_timing[i].total_ticks * ns_per_tick);
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

    if (trace_set_item(entry, "count",
            PyLong_FromUnsignedLongLong(c)) < 0 ||
        trace_set_item(entry, "total_ns",
            PyLong_FromUnsignedLongLong(t)) < 0 ||
        trace_set_item(entry, "avg_ns",
            PyLong_FromUnsignedLongLong(avg)) < 0 ||
        trace_set_item(entry, "share_of_time",
            PyFloat_FromDouble(share_time)) < 0 ||
        trace_set_item(entry, "share_of_executions",
            PyFloat_FromDouble(share_exec)) < 0 ||
        trace_set_item(entry, "ratio_vs_mean",
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

  if (trace_set_item(summary, "total_executions",
          PyLong_FromUnsignedLongLong(total_executions)) < 0 ||
      trace_set_item(summary, "total_ns",
          PyLong_FromUnsignedLongLong(total_ns_all)) < 0 ||
      trace_set_item(summary, "avg_ns_per_dispatch",
          PyLong_FromUnsignedLongLong(avg_ns_per_dispatch)) < 0 ||
      trace_set_item(summary, "handler_count_nonzero",
          PyLong_FromUnsignedLong((unsigned long) handler_count_nonzero)) < 0 ||
      trace_set_item(summary, "max_total_ns_handler",
          PyUnicode_FromString(max_total_ns_handler)) < 0 ||
      trace_set_item(summary, "max_total_ns",
          PyLong_FromUnsignedLongLong(max_total_ns)) < 0 ||
      trace_set_item(summary, "max_avg_ns_handler",
          PyUnicode_FromString(max_avg_ns_handler)) < 0 ||
      trace_set_item(summary, "max_avg_ns",
          PyLong_FromUnsignedLongLong(max_avg_ns)) < 0 ||
      trace_set_item(summary, "rollup_invocations",
          PyLong_FromUnsignedLongLong(rollup_count)) < 0 ||
      trace_set_item(summary, "rollup_total_ns",
          PyLong_FromUnsignedLongLong(rollup_total_ns)) < 0 ||
      trace_set_item(summary, "rollup_avg_ns",
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
        if (trace_set_item(row, "handler",
                PyUnicode_FromString(hname)) < 0 ||
            trace_set_item(row, "total_ns",
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
        if (trace_set_item(row, "handler",
                PyUnicode_FromString(hname)) < 0 ||
            trace_set_item(row, "avg_ns",
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
      PyDict_SetItemString(flags, "opcache",
          KOS_TRACE_OPCACHE ? Py_True : Py_False) < 0 ||
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

  if (add_opcache_section(report) < 0) {
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
