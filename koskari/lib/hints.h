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


#ifndef KOSKARI_FEATURES_H
#define KOSKARI_FEATURES_H


/**
   features.h

   Feature definitions for Koskari.

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


/* Internal-invariant safety net. Distinct from user-input validation: these
   guards catch koskari's own state-machine bugs. Off by default -- past
   assembly the evaluator trusts its own state. Build with
   -DKOS_EVAL_SAFETYNET=1 for clean diagnostics instead of a segfault. */
#ifndef KOS_EVAL_SAFETYNET
# define KOS_EVAL_SAFETYNET 0
#endif


#endif


// The end.
