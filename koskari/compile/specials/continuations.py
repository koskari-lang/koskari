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
from ...lib import OpCode, cons, nil, symbol
from ...matcher import BODY, CALLEDBY, MATCH, REST, SYMBOL, VALUE
from ...syntax import special
from ...types import is_keyword, strip_mark
from ..helpers import helper_begin


_symbol_values = symbol("values")


_ONE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("expr")),
]

_K_BODY_PATTERNS = [
    MATCH(CALLEDBY("called_by"), SYMBOL("k"), BODY("body")),
]

_BODY_PATTERNS = [
    MATCH(CALLEDBY("called_by"), BODY("body")),
]

_VALUES_PATTERNS = [
    MATCH(CALLEDBY("called_by"), REST("rest")),
]

_OPTIONAL_ONE_PATTERNS = [
    MATCH(CALLEDBY("called_by")),
    MATCH(CALLEDBY("called_by"), VALUE("expr")),
]


@builtin("compile/call/cc")
@special("compile/call/cc", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_call_cc(compiler, called_by, expr):
    """
    Compile (compile/call/cc FN). Captures continuation, invokes FN with the
    continuation, and returns the result.
    """

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    fragment.instruction(OpCode.PUSH_CC, 0)
    fragment.merge(compiler.full_compile(expr.datum))
    fragment.instruction(OpCode.CAPTURE_CC, 0)
    fragment.instruction(OpCode.CALL, 1)
    fragment.instruction(OpCode.POP_CC, 0)

    return fragment


@builtin("compile/let/cc")
@special("compile/let/cc", patterns=_K_BODY_PATTERNS, unpack="kwargs")
def special_let_cc(compiler, called_by, k, body):
    """
    Compile (compile/let/cc K BODY...). Pushes new namespace to hold K,
    captures continuation, binds to K, runs body. K must be a symbol
    """

    k_name = str(k.datum)

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)

    fragment.instruction(OpCode.PUSH_CC, 0)
    with fragment.child_namespace(called_by.datum, [], []) as frag:
        frag.declare_var(k_name)
        frag.instruction(OpCode.CAPTURE_CC, 0)
        frag.instruction(OpCode.VAR_STORE, k_name)
        helper_begin(frag, body.datum)
    fragment.instruction(OpCode.POP_CC, 0)

    return fragment


@builtin("compile/capture/cc")
@special("compile/capture/cc", patterns=_K_BODY_PATTERNS, unpack="kwargs")
def special_capture_cc(compiler, called_by, k, body):
    """
    Compile (compile/capture/cc K BODY...). Captures continuation, binds to K,
    runs body.

    Intentionally does not open a child namespace: K is bound directly into
    the enclosing scope so the continuation remains reachable after the body
    exits. This is the semantic distinction from compile/let/cc, which
    introduces a fresh child namespace that is discarded once its body
    completes.
    """

    k_name = str(k.datum)

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    fragment.instruction(OpCode.PUSH_CC, 0)
    fragment.instruction(OpCode.CAPTURE_CC, 0)
    fragment.instruction(OpCode.VAR_STORE, k_name)
    helper_begin(fragment, body.datum)
    fragment.instruction(OpCode.POP_CC, 0)

    return fragment


@builtin("compile/reset")
@special("compile/reset", patterns=_BODY_PATTERNS, unpack="kwargs")
def special_reset(compiler, called_by, body):
    """
    Compile (compile/reset EXPR...). Establishes a prompt (tag 0), runs body,
    pops prompt. Enables delimited continuations via compile/shift.
    """

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    fragment.instruction(OpCode.PUSH_PROMPT, 0)
    helper_begin(fragment, body.datum)
    fragment.instruction(OpCode.POP_PROMPT, 0)

    return fragment


@builtin("compile/shift")
@special("compile/shift", patterns=_K_BODY_PATTERNS, unpack="kwargs")
def special_shift(compiler, called_by, k, body):
    """
    Compile (compile/shift K EXPR...). Captures continuation, binds to K, runs
    handler body. K must be a symbol; tag 0.
    """

    k_name = str(k.datum)

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    fragment.instruction(OpCode.PUSH_SHIFT, 0)
    fragment.declare_var(k_name)
    fragment.instruction(OpCode.VAR_STORE, k_name)
    helper_begin(fragment, body.datum)
    fragment.instruction(OpCode.POP_SHIFT, 0)

    return fragment


def emit_values_form(compiler, source, opcode):
    """
    Compile a form that delivers zero, one, or several values, as
    return and yield do: each value in turn, then opcode with the
    count. A keyword among the values makes it a values form instead:
    the source is rewritten to (KIND (values ...)) and returned for the
    compiler to expand

    :param compiler: the Compiler
    :param source: the whole form; called-by at its head
    :param opcode: RETURN or YIELD
    :return: the fragment, or the rewritten source
    """

    called_by, rest = source
    frag = compiler.fragment(called_by)

    count = 0
    while rest:
        if is_keyword(strip_mark(rest.car)):
            # discard work in progress and return as a transform to
            # invoke the form on the values instead.
            fixup = cons(cons(_symbol_values, source.cdr), nil)
            source.cdr = fixup
            return source

        frag.position_of(rest)
        frag.merge(compiler.full_compile(rest.car))

        rest = rest.cdr
        count += 1

    frag.position_of(source)
    frag.instruction(opcode, count)
    return frag


@builtin("compile/return")
@special("compile/return", patterns=_VALUES_PATTERNS)
def special_return(compiler, source):
    """
    Returns from function.

    (return)            ; returns None
    (return val)        ; returns val
    (return vals ...)   ; returns tuple of vals
    (return k: v ...)   ; returns values object over complex arguments
    """

    return emit_values_form(compiler, source, OpCode.RETURN)


@builtin("compile/yield")
@special("compile/yield", patterns=_VALUES_PATTERNS)
def special_yield(compiler, source):
    """
    Yields from function.

    (yield)            ; yields None
    (yield val)        ; yields val
    (yield vals ...)   ; yields tuple of vals
    (yield k: v ...)   ; yields values object over complex arguments
    """

    return emit_values_form(compiler, source, OpCode.YIELD)


@builtin("compile/await")
@special("compile/await", patterns=_OPTIONAL_ONE_PATTERNS, unpack="match")
def special_await(compiler, caps):
    """
    Compile (compile/await) or (compile/await EXPR). Zero-arg: bare AWAIT (0)
    then CONST None. One-arg: evaluate EXPR, then AWAIT (1) on TOS.
    """

    called_by = caps["called_by"]

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)
    if "expr" in caps:
        frag.merge(compiler.full_compile(caps["expr"].datum))
        frag.instruction(OpCode.AWAIT, 1)
    else:
        frag.instruction(OpCode.AWAIT, 0)
    return frag


@builtin("compile/yield-from")
@special("compile/yield-from", patterns=_ONE_PATTERNS, unpack="kwargs")
def special_yield_from(compiler, called_by, expr):
    """
    Compile (compile/yield-from EXPR). Evaluates EXPR, then yields from TOS via
    YIELD_FROM.
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(called_by.position)
    frag.merge(compiler.full_compile(expr.datum))
    frag.instruction(OpCode.YIELD_FROM, 0)
    frag.instruction(OpCode.CONST, None)
    return frag


# The end.
