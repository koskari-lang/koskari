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


#ifndef KOSKARI_TRACING_H
#define KOSKARI_TRACING_H


#include <Python.h>
#include <stdint.h>


/**
   tracing.h

   Compile-flag enabled tracing for Koskari freelists, interned atoms,
   events, and timing. Include this header in .c files that use tracing
   macros. Not included from lib.h; opt-in per file (same pattern as
   debug.h).

   Event counters use KosTraceEventId; increment with KOS_TRACE_EVENTS_INC.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


/* === event counter ids (add new ids only at end) === */

typedef enum KosTraceEventId {
  KOS_TE_DYN_HANDLE_ENTER = 0,
  KOS_TE_DYN_UNWIND_STEP,
  KOS_TE_TRANSFER_PLAN_BUILD,
  KOS_TE_RAISE_LOCAL_WALK_ITER,
  KOS_TE__COUNT
} KosTraceEventId;


/* === compile-time flags === */

#ifndef KOS_TRACE_FREELIST
#define KOS_TRACE_FREELIST 0
#endif

#ifndef KOS_TRACE_INTERN
#define KOS_TRACE_INTERN 0
#endif

#ifndef KOS_TRACE_EVENTS
#define KOS_TRACE_EVENTS 0
#endif

#ifndef KOS_TRACE_TIMING
#define KOS_TRACE_TIMING 0
#endif


/* === API (always available) === */

void kos_trace_init(void);
void kos_trace_clear(void);
PyObject *kos_trace_report(void);


#if KOS_TRACE_EVENTS
void kos_trace_events_inc(KosTraceEventId id);
#endif


#if KOS_TRACE_TIMING
uint64_t kos_trace_now_ns(void);
void kos_trace_opcode_record(unsigned op, uint64_t delta_ns);
#endif


/* === freelist tracing (declarations when enabled) === */

#if KOS_TRACE_FREELIST
void kos_trace_fl_alloc(const char *name, int from_freelist);
void kos_trace_fl_return(const char *name, int overrun);
#endif


/* === freelist tracing macros === */

#if KOS_TRACE_FREELIST
#define KOS_TRACE_FL_ALLOC(name, from_freelist)   \
  kos_trace_fl_alloc(name, from_freelist)
#define KOS_TRACE_FL_RETURN(name, overrun)        \
  kos_trace_fl_return(name, overrun)
#else
#define KOS_TRACE_FL_ALLOC(name, from_freelist)   ((void)0)
#define KOS_TRACE_FL_RETURN(name, overrun)        ((void)0)
#endif


/* === intern tracing macros (phase 2) === */

#if KOS_TRACE_INTERN
#define KOS_TRACE_INTERN_HIT(kind, hit)           \
  kos_trace_intern_hit(kind, hit)
#else
#define KOS_TRACE_INTERN_HIT(kind, hit)           ((void)0)
#endif


/* === events tracing macros === */

#if KOS_TRACE_EVENTS
#define KOS_TRACE_EVENTS_INC(id) kos_trace_events_inc(id)
#else
#define KOS_TRACE_EVENTS_INC(id) ((void)0)
#endif


/* === per-opcode timing (eval dispatch) === */

#if KOS_TRACE_TIMING
#define KOS_TRACE_OP_WRAP(name, stmt)                                       \
  do {                                                                      \
    uint64_t _kos_t0 = kos_trace_now_ns();                                  \
    stmt;                                                                   \
    kos_trace_opcode_record((unsigned)(KOS_OP_##name),                        \
                            kos_trace_now_ns() - _kos_t0);                   \
  } while (0)
#else
#define KOS_TRACE_OP_WRAP(name, stmt) do { stmt; } while (0)
#endif


#endif


// The end.
