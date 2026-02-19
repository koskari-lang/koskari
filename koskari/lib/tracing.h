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


/**
   tracing.h

   Compile-flag enabled tracing for Koskari freelists, interned atoms,
   events, and timing. Include this header in .c files that use tracing
   macros. Not included from lib.h; opt-in per file (same pattern as
   debug.h).

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


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


/* === events tracing macros (phase 2) === */

#if KOS_TRACE_EVENTS
#define KOS_TRACE_EVENT(name)                     \
  kos_trace_event(name)
#else
#define KOS_TRACE_EVENT(name)                     ((void)0)
#endif


/* === timing tracing macros (phase 2) === */

#if KOS_TRACE_TIMING
#define KOS_TRACE_TIMING_START(name)              \
  kos_trace_timing_start(name)
#define KOS_TRACE_TIMING_STOP(name)               \
  kos_trace_timing_stop(name)
#else
#define KOS_TRACE_TIMING_START(name)              ((void)0)
#define KOS_TRACE_TIMING_STOP(name)               ((void)0)
#endif


#endif


// The end.
