# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
koskari.compile.specials.continuations

Special form handlers for compile/call/cc, compile/let/cc,
compile/capture/cc, compile/reset, compile/shift, compile/return,
compile/yield, and compile/await.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode, cons, nil, symbol
from ...matcher import CALLEDBY, MATCH, REST
from ...syntax import special
from ...types import is_keyword, is_nil, is_pair
from ..helpers import helper_begin, is_symbolish


_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)

_symbol_values = symbol("values")


@builtin("compile/call/cc")
@special("compile/call/cc", patterns=_DEFAULT_PATTERNS)
def special_call_cc(compiler, source):
    """
    Compile (compile/call/cc FN). Captures continuation, invokes FN with the
    continuation, and returns the result.
    """

    called_by, args = source
    if is_nil(args) or not is_pair(args):
        raise SpecialError("call/cc requires a single argument", source)
    expr, rest = args
    if not is_nil(rest):
        raise SpecialError("call/cc requires a single argument", source)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.instruction(OpCode.PUSH_CC, 0)
    fragment.merge(compiler.full_compile(expr))
    fragment.instruction(OpCode.CAPTURE_CC, 0)
    fragment.instruction(OpCode.CALL, 1)
    fragment.instruction(OpCode.POP_CC, 0)

    return fragment


@builtin("compile/let/cc")
@special("compile/let/cc", patterns=_DEFAULT_PATTERNS)
def special_let_cc(compiler, source):
    """
    Compile (compile/let/cc K BODY...). Pushes new namespace to hold K,
    captures continuation, binds to K, runs body. K must be a symbol
    """

    called_by, (k, body) = source
    if not is_symbolish(k):
        raise SpecialError(
            "let/cc continuation variable must be a symbol",
            source)

    k_name = str(k)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)

    fragment.instruction(OpCode.PUSH_CC, 0)
    with fragment.child_namespace(called_by, [], []) as frag:
        frag.declare_var(k_name)
        frag.instruction(OpCode.CAPTURE_CC, 0)
        frag.instruction(OpCode.VAR_STORE, k_name)
        helper_begin(frag, body)
    fragment.instruction(OpCode.POP_CC, 0)

    return fragment


@builtin("compile/capture/cc")
@special("compile/capture/cc", patterns=_DEFAULT_PATTERNS)
def special_capture_cc(compiler, source):
    """
    Compile (compile/capture/cc K BODY...). Captures continuation, binds to K,
    runs body.

    Intentionally does not open a child namespace: K is bound directly into
    the enclosing scope so the continuation remains reachable after the body
    exits. This is the semantic distinction from compile/let/cc, which
    introduces a fresh child namespace that is discarded once its body
    completes.
    """

    called_by, (k, body) = source
    if not is_symbolish(k):
        raise SpecialError(
            "capture/cc continuation variable must be a symbol",
            source)

    k_name = str(k)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.instruction(OpCode.PUSH_CC, 0)
    fragment.instruction(OpCode.CAPTURE_CC, 0)
    fragment.instruction(OpCode.VAR_STORE, k_name)
    helper_begin(fragment, body)
    fragment.instruction(OpCode.POP_CC, 0)

    return fragment


@builtin("compile/reset")
@special("compile/reset", patterns=_DEFAULT_PATTERNS)
def special_reset(compiler, source):
    """
    Compile (compile/reset EXPR...). Establishes a prompt (tag 0), runs body,
    pops prompt. Enables delimited continuations via compile/shift.
    """

    called_by, rest = source
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError("reset requires body", source)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.instruction(OpCode.PUSH_PROMPT, 0)
    helper_begin(fragment, rest)
    fragment.instruction(OpCode.POP_PROMPT, 0)

    return fragment


@builtin("compile/shift")
@special("compile/shift", patterns=_DEFAULT_PATTERNS)
def special_shift(compiler, source):
    """
    Compile (compile/shift K EXPR...). Captures continuation, binds to K, runs
    handler body. K must be a symbol; tag 0.
    """

    called_by, rest = source
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError(
            "shift requires continuation variable and body",
            source)

    k, body = rest
    if not is_symbolish(k):
        raise SpecialError(
            "shift continuation variable must be a symbol",
            source)

    k_name = str(k)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.instruction(OpCode.PUSH_SHIFT, 0)
    fragment.declare_var(k_name)
    fragment.instruction(OpCode.VAR_STORE, k_name)
    helper_begin(fragment, body)
    fragment.instruction(OpCode.POP_SHIFT, 0)

    return fragment


@builtin("compile/return")
@special("compile/return", patterns=_DEFAULT_PATTERNS)
def special_return(compiler, source):
    """
    Returns from function.

    (return)            ; returns None
    (return val)        ; returns val
    (return vals ...)   ; returns tuple of vals
    (return k: v ...)   ; returns values object over complex arguments
    """

    called_by, rest = source
    frag = compiler.fragment(called_by)

    count = 0
    while rest:
        if is_keyword(rest.car):
            # discard work in progress and return as a transform to
            # invoke return on the values instead.
            fixup = cons(cons(_symbol_values, source.cdr), nil)
            source.cdr = fixup
            return source

        frag.position_of(rest)
        frag.merge(compiler.full_compile(rest.car))

        rest = rest.cdr
        count += 1

    frag.position_of(source)
    frag.instruction(OpCode.RETURN, count)
    return frag


@builtin("compile/yield")
@special("compile/yield", patterns=_DEFAULT_PATTERNS)
def special_yield(compiler, source):
    """
    Yields from function.

    (yield)            ; yields None
    (yield val)        ; yields val
    (yield vals ...)   ; yields tuple of vals
    (yield k: v ...)   ; yields values object over complex arguments
    """

    called_by, rest = source
    frag = compiler.fragment(called_by)

    count = 0
    while rest:
        if is_keyword(rest.car):
            # discard work in progress and return as a transform to
            # invoke yield on the values instead.
            fixup = cons(cons(_symbol_values, source.cdr), nil)
            source.cdr = fixup
            return source

        frag.position_of(rest)
        frag.merge(compiler.full_compile(rest.car))

        rest = rest.cdr
        count += 1

    frag.position_of(source)
    frag.instruction(OpCode.YIELD, count)
    return frag


@builtin("compile/await")
@special("compile/await", patterns=_DEFAULT_PATTERNS)
def special_await(compiler, source):
    """
    Compile (compile/await) or (compile/await EXPR). Zero-arg: bare AWAIT (0)
    then CONST None. One-arg: evaluate EXPR, then AWAIT (1) on TOS.
    """

    called_by, rest = source
    expr = nil
    if not is_nil(rest):
        expr, tail = rest
        if not is_nil(tail):
            raise SpecialError("await requires zero or one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    if expr:
        frag.merge(compiler.full_compile(expr))
        frag.instruction(OpCode.AWAIT, 1)
    else:
        frag.instruction(OpCode.AWAIT, 0)
    return frag


@builtin("compile/yield-from")
@special("compile/yield-from", patterns=_DEFAULT_PATTERNS)
def special_yield_from(compiler, source):
    """
    Compile (compile/yield-from EXPR). Evaluates EXPR, then yields from TOS via
    YIELD_FROM.
    """

    called_by, rest = source
    if is_nil(rest) or not is_pair(rest):
        raise SpecialError("yield-from requires a single argument", source)

    expr, tail = rest
    if not is_nil(tail):
        raise SpecialError("yield-from requires a single argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.YIELD_FROM, 0)
    frag.instruction(OpCode.CONST, None)
    return frag


# The end.
