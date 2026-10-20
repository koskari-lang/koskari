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

   Compile-flag enabled tracing for Koskari freelists, events, and timing.
   Include this header in .c files that use tracing macros. Not included from
   lib.h; opt-in per file (same pattern as debug.h). eval.h includes this
   header when instrumenting the live register sync steps with
   KOS_TRACE_EVAL_SYNC.

   Event counters use KosTraceEventId; increment with KOS_TRACE_EVENTS_INC.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


/* === event counter ids (add new ids only at end) === */

typedef enum KosTraceEventId {
  KOS_TE_DYN_HANDLE_ENTER = 0,
  KOS_TE_DYN_UNWIND_STEP,
  KOS_TE_TRANSFER_GOAL_NEW,
  KOS_TE_RAISE_LOCAL_WALK_ITER,
  KOS_TE_SEG_INLINE_INSTALL,
  KOS_TE_SEG_INLINE_SPILL,
  KOS_TE_RETARGET_WALK_FALLBACK,
  KOS_TE_PARENT_SLOT_WALK,
  KOS_TE_LIVE_REBUILD,
  /* call-class routing: frame_cached = simple call served by a cached
     frame; simple_steal = steal-bind into a fresh namespace; simple_vector
     = simple call bound from a vector (no steal, not cacheable);
     tail_inplace = self-tail call rebinding the live frame's namespace. */
  KOS_TE_CALL_FRAME_CACHED,
  KOS_TE_CALL_SIMPLE_STEAL,
  KOS_TE_CALL_SIMPLE_VECTOR,
  KOS_TE_CALL_TAIL_INPLACE,
  KOS_TE__COUNT
} KosTraceEventId;


/**
   Per-site opcode-cache counter ids (KOS_TRACE_OPCACHE). These are distinct
   from the eval-loop event counters: they measure cache behavior (hit rate,
   churn) per cache family, and grow as new caches land. Add new ids only at
   the end. See design/archive/attr-lookup-cache.md.

   Method cache: hit = guarded slot served without a full resolve; miss = slot
   present but re-resolved (cold, guard miss, value moved); bypass =
   megamorphic site skipped caching; uncached = no slot (gated off).
*/
typedef enum KosTraceOpCacheId {
  KOS_OC_METHOD_HIT = 0,
  KOS_OC_METHOD_MISS,
  KOS_OC_METHOD_BYPASS,
  KOS_OC_METHOD_UNCACHED,
  /* field (attribute-storage location) cache: hit = guarded location-mode
     served (e.g. SLOT); generic = guard-free GENERIC verdict (fell to the
     generic path); miss = cold classification (cold slot, guard miss);
     fallback = guarded hit whose direct access then declined (values
     inactive, unset slot) and fell to the generic protocol -- a healthy
     workload keeps this near zero. */
  KOS_OC_FIELD_HIT,
  KOS_OC_FIELD_GENERIC,
  KOS_OC_FIELD_MISS,
  KOS_OC_FIELD_FALLBACK,
  /* elector: hit = an elected body's guard held; miss = an election or
     a guard miss; bypass = a site demoted to its generic body;
     uncached = the site has no slot. */
  KOS_OC_ELECTOR_HIT,
  KOS_OC_ELECTOR_MISS,
  KOS_OC_ELECTOR_BYPASS,
  KOS_OC_ELECTOR_UNCACHED,
  KOS_OC__COUNT
} KosTraceOpCacheId;


/**
   Post-opcode eval handler timing ids (KOS_TRACE_TIMING). Add new ids only at
   end. Names must match KOS_TRACE_POST_OP_WRAP(name, ...) token pasting
   (KOS_TP_##name).
*/
typedef enum KosTracePostOpId {
  KOS_TP_rollup = 0,
  KOS_TP_parent_return,
  KOS_TP_control_returned,
  KOS_TP_event_raise,
  KOS_TP_event_await,
  KOS_TP_event_yield,
  KOS_TP_return_event,
  KOS_TP_plain_return,
  KOS_TP__COUNT
} KosTracePostOpId;


/* === compile-time flags === */

#ifndef KOS_TRACE_FREELIST
#define KOS_TRACE_FREELIST 0
#endif

#ifndef KOS_TRACE_EVENTS
#define KOS_TRACE_EVENTS 0
#endif

#ifndef KOS_TRACE_OPCACHE
#define KOS_TRACE_OPCACHE 0
#endif

#ifndef KOS_TRACE_TIMING
#define KOS_TRACE_TIMING 0
#endif

#ifndef KOS_TRACE_EVAL_SYNC
#define KOS_TRACE_EVAL_SYNC 0
#endif

#ifndef KOS_DEBUG_PATCH_DYN_CACHES
#define KOS_DEBUG_PATCH_DYN_CACHES 0
#endif


/* === eval state sync timing ids (KOS_TRACE_EVAL_SYNC; add only at end) === */

typedef enum KosTraceEvalSyncId {
  KOS_TEVS_init = 0,
  KOS_TEVS_unused_sync_from_context,
  KOS_TEVS_flush_live_regs,
  KOS_TEVS_unused_apply_dyn_caches,
  KOS_TEVS_rebuild_live_from_chain,
  KOS_TEVS_retarget_after_push,
  KOS_TEVS__COUNT
} KosTraceEvalSyncId;


/* === API (always available) === */

void kos_trace_init(void);
void kos_trace_clear(void);
PyObject *kos_trace_report(void);


#if KOS_TRACE_EVENTS
void kos_trace_events_inc(KosTraceEventId id);
#endif


#if KOS_TRACE_OPCACHE
void kos_trace_opcache_inc(KosTraceOpCacheId id);
#endif


#if KOS_TRACE_TIMING
/* Timing slots: one per opcode, then the pseudo slots for the time a
   thread spends in a resume chain and in the trampoline itself (the
   prologue, the poll, and the result handling between two chains). */
#define KOS_TRACE_SLOT_RESUME      ((unsigned) KOS_OP_THEEND + 1u)
#define KOS_TRACE_SLOT_TRAMPOLINE  ((unsigned) KOS_OP_THEEND + 2u)
#define KOS_TRACE_SLOT_COUNT       ((unsigned) KOS_OP_THEEND + 3u)

uint64_t kos_trace_now_ns(void);
uint64_t kos_trace_ticks(void);
void kos_trace_step(unsigned slot);
void kos_trace_exit(void);
void kos_trace_eval_push(void);
void kos_trace_eval_pop(void);
void kos_trace_post_op_enter(void);
void kos_trace_post_op_exit(void);
void kos_trace_post_op_record(unsigned id, uint64_t delta);
#endif


#if KOS_TRACE_EVAL_SYNC
uint64_t kos_trace_eval_sync_now(void);
void kos_trace_eval_sync_record(KosTraceEvalSyncId id, uint64_t delta_ns);
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


/* === events tracing macros === */

#if KOS_TRACE_EVENTS
#define KOS_TRACE_EVENTS_INC(id) kos_trace_events_inc(id)
#else
#define KOS_TRACE_EVENTS_INC(id) ((void)0)
#endif


/* === opcode-cache tracing macros === */

#if KOS_TRACE_OPCACHE
#define KOS_TRACE_OPCACHE_INC(id) kos_trace_opcache_inc(id)
#else
#define KOS_TRACE_OPCACHE_INC(id) ((void)0)
#endif


/* === per-opcode timing (threaded dispatch) === */

/*
   Under threaded dispatch nothing returns between two handlers, so an
   opcode is timed by its boundaries rather than around a call: one
   timing slot is open per thread, and every transfer closes the open
   slot and opens the next. The transfer macros carry the step (a fetch
   knows the opcode it enters), a resume hop steps into the resume
   slot, and the trampoline slot is stepped into at the eval entry and
   at the entry of each signal service (the poll, the raise walk, the
   await, a general-path return, a terminal yield, control returned, a
   staged dispatch), which is also what closes the handler that tailed
   into the service. A nested
   evaluator pushes the open slot at entry and pops it at exit, so each
   opcode reads its exclusive time. The clock is a cycle counter where
   one is available; the report calibrates it to nanoseconds.
*/

#if KOS_TRACE_TIMING
#define KOS_TRACE_THREAD_STEP(op)     kos_trace_step((unsigned) (op))
#define KOS_TRACE_RESUME_STEP()       kos_trace_step(KOS_TRACE_SLOT_RESUME)
#define KOS_TRACE_TRAMPOLINE_STEP()   kos_trace_step(KOS_TRACE_SLOT_TRAMPOLINE)
#define KOS_TRACE_OP_EXIT()           kos_trace_exit()
#define KOS_TRACE_EVAL_PUSH()         kos_trace_eval_push()
#define KOS_TRACE_EVAL_POP()          kos_trace_eval_pop()

#define KOS_TRACE_POST_OP_ENTER() kos_trace_post_op_enter()
#define KOS_TRACE_POST_OP_EXIT() kos_trace_post_op_exit()
#define KOS_TRACE_POST_OP_WRAP(name, stmt)                                  \
  do {                                                                      \
    uint64_t _kos_pt0 = kos_trace_ticks();                                  \
    stmt;                                                                   \
    kos_trace_post_op_record((unsigned)(KOS_TP_##name),                       \
                             kos_trace_ticks() - _kos_pt0);                  \
  } while (0)
#else
#define KOS_TRACE_THREAD_STEP(op)     ((void) 0)
#define KOS_TRACE_RESUME_STEP()       ((void) 0)
#define KOS_TRACE_TRAMPOLINE_STEP()   ((void) 0)
#define KOS_TRACE_OP_EXIT()           ((void) 0)
#define KOS_TRACE_EVAL_PUSH()         ((void) 0)
#define KOS_TRACE_EVAL_POP()          ((void) 0)
#define KOS_TRACE_POST_OP_ENTER() ((void)0)
#define KOS_TRACE_POST_OP_EXIT() ((void)0)
#define KOS_TRACE_POST_OP_WRAP(name, stmt) do { stmt; } while (0)
#endif


/* === eval state sync (counts + wall time in get_trace_report) === */

#if KOS_TRACE_EVAL_SYNC
#define KOS_TRACE_EVAL_SYNC_BEGIN() \
  uint64_t _kos_evs_t0 = kos_trace_eval_sync_now()
#define KOS_TRACE_EVAL_SYNC_END(id)                                         \
  kos_trace_eval_sync_record((KosTraceEvalSyncId)(id),                      \
                             kos_trace_eval_sync_now() - _kos_evs_t0)
#else
#define KOS_TRACE_EVAL_SYNC_BEGIN() ((void)0)
#define KOS_TRACE_EVAL_SYNC_END(id) ((void)0)
#endif


#endif


// The end.
