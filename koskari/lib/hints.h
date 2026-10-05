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


#ifndef KOSKARI_HINTS_H
#define KOSKARI_HINTS_H


/**
   hints.h

   Compiler hint and build knob defaults for Koskari.

   :author: Christopher O'Brien <obriencj@preoccupied.net>
   :license: GNU General Public License v3
*/


/* KOS_OP_INLINE marks the small hot cores that must fold into their
   callers (the operator cores, the chain-segment walk).

   KOS_ALL_COLD=1 builds a measurement variant instead: every such
   core is un-inlined so each is its own symbol. Never ship this. */
#ifndef KOS_ALL_COLD
# define KOS_ALL_COLD 0
#endif

#if defined(__GNUC__) || defined(__clang__)
# define KOS_NOINLINE __attribute__((noinline))
# if KOS_ALL_COLD
#  define KOS_OP_INLINE static KOS_NOINLINE
# else
#  define KOS_OP_INLINE static inline __attribute__((always_inline))
# endif
# define likely(x)   __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define KOS_OP_INLINE static inline
# define KOS_NOINLINE
# define likely(x)   (x)
# define unlikely(x) (x)
#endif


/* Whether the compiler can enforce a tail call (clang, or GCC 15+).
   Opcode dispatch is threaded through musttail, so eval.h refuses to
   build without it. */
#if defined(__has_attribute)
# if __has_attribute(musttail)
#  define KOS_HAVE_MUSTTAIL 1
# endif
#endif
#ifndef KOS_HAVE_MUSTTAIL
# define KOS_HAVE_MUSTTAIL 0
#endif


/* Calling convention for threaded steps (clang 19+): the function
   preserves no registers for its caller and takes its arguments in
   the registers a normal callee would have to save. A step never
   returns into a frame that wants registers kept, and the trampoline
   is the one plain caller, so the tail chain sheds every register
   spill. Empty where the attribute is unknown or the target cannot
   honor it: clang knows the attribute on every target but emits the
   convention only on x86-64, and on AArch64 from clang 19; elsewhere
   it warns, which -Werror turns into a failed build. */
#if defined(__has_attribute) \
    && (defined(__x86_64__) \
        || (defined(__aarch64__) && defined(__clang_major__) \
            && __clang_major__ >= 19))
# if __has_attribute(preserve_none)
#  define KOS_PRESERVE_NONE __attribute__((preserve_none))
# endif
#endif
#ifndef KOS_PRESERVE_NONE
# define KOS_PRESERVE_NONE
#endif


/* Internal-invariant safety net. Distinct from user-input validation: these
   guards catch koskari's own state-machine bugs. Off by default -- past
   assembly the evaluator trusts its own state. Build with
   -DKOS_EVAL_SAFETYNET=1 for clean diagnostics instead of a segfault. */
#ifndef KOS_EVAL_SAFETYNET
# define KOS_EVAL_SAFETYNET 0
#endif


#endif


// The end.
