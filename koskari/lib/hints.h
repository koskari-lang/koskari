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


/* KOS_LOOP_SCALAR, written on the line before a for loop, keeps the
   compiler from vectorising or unrolling it. At -O2 GCC and clang turn
   a pointer-copy loop of unknown trip count into a length threshold,
   an overlap test, an SSE loop, a four-way unrolled loop and a
   remainder loop: about 240 bytes, and 14 + 5n instructions executed
   for n pointers. The loops this marks copy one to three pointers on
   the call path, where the plain 3n-instruction loop is both smaller
   and faster. "GCC novector" exists from GCC 14; "GCC unroll 0" from
   GCC 8; other compilers get the plain loop they would emit anyway. */
#if defined(__clang__)
# define KOS_LOOP_SCALAR \
  _Pragma("clang loop vectorize(disable) interleave(disable) unroll(disable)")
#elif defined(__GNUC__) && __GNUC__ >= 14
# define KOS_LOOP_SCALAR _Pragma("GCC novector") _Pragma("GCC unroll 0")
#elif defined(__GNUC__)
# define KOS_LOOP_SCALAR _Pragma("GCC unroll 0")
#else
# define KOS_LOOP_SCALAR
#endif


/* KOS_TLS_INITIAL_EXEC, written after a thread-local's declarator,
   gives it the initial-exec TLS model. A -fPIC build otherwise takes
   the dynamic model, which calls __tls_get_addr on each access. Valid
   here because the interpreter loads the extension once, and a few
   pointers fit in the C library's static TLS surplus. */
#if defined(__GNUC__) || defined(__clang__)
# define KOS_TLS_INITIAL_EXEC __attribute__((tls_model("initial-exec")))
#else
# define KOS_TLS_INITIAL_EXEC
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
