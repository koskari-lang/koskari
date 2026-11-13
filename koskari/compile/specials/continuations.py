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
compile/capture/cc, compile/reset, compile/reset-at, compile/shift,
compile/shift-at, compile/return, compile/yield, compile/await, and
compile/yield-from.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode, cons, nil, symbol
from ...matcher import BODY, CALLEDBY, MATCH, REST, SYMBOL, VALUE
from ...syntax import special
from ...types import is_keyword
from ..helpers import helper_begin
from ..processor import YIELD_OPS


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

_TAG_BODY_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("tag"), BODY("body")),
]

_TAG_K_BODY_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("tag"), SYMBOL("k"), BODY("body")),
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
    # the callee is set aside for the capture, so the continuation's
    # stack holds the form's enclosing operands and nothing of its own
    fragment.instruction(OpCode.CAPTURE_CC, 1)
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

    k_sym = k.datum

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)

    fragment.instruction(OpCode.PUSH_CC, 0)
    with fragment.child_namespace(called_by.datum, [], []) as frag:
        frag.declare_var(k_sym)
        frag.instruction(OpCode.CAPTURE_CC, 0)
        frag.instruction(OpCode.VAR_STORE, k_sym)
        helper_begin(frag, body.datum)
    fragment.instruction(OpCode.POP_CC, 0)

    return fragment


@builtin("compile/capture/cc")
@special("compile/capture/cc", patterns=_K_BODY_PATTERNS, unpack="kwargs")
def special_capture_cc(compiler, called_by, k, body):
    """
    Compile (compile/capture/cc K BODY...). Captures continuation, declare
    and stores to K, runs body.

    Intentionally does not open a child namespace: K is bound directly into
    the enclosing scope so the continuation remains reachable after the body
    exits. This is the semantic distinction from compile/let/cc, which
    introduces a fresh child namespace that is discarded once its body
    completes.
    """

    fragment = compiler.fragment(called_by.datum)

    k_sym = k.datum
    fragment.declare_var(k_sym, position=k.position)

    fragment.position(called_by.position)
    fragment.instruction(OpCode.PUSH_CC, 0)
    fragment.instruction(OpCode.CAPTURE_CC, 0)
    fragment.instruction(OpCode.VAR_STORE, k_sym)
    helper_begin(fragment, body.datum)
    fragment.instruction(OpCode.POP_CC, 0)

    return fragment


@builtin("compile/reset")
@special("compile/reset", patterns=_BODY_PATTERNS, unpack="kwargs")
def special_reset(compiler, called_by, body):
    """
    Compile (compile/reset EXPR...). The body runs in a frame of its
    own above a prompt of the default tag; its value, or the value of
    a compile/shift handler that aborts to the prompt, is the reset's
    """

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    helper_reset_body(compiler, called_by, body, fragment, 0)

    return fragment


def refuse_yield(called_by, instructions, refusal):
    """
    Raise SpecialError when instructions hold a yield or yield-from

    :param called_by: the special's called-by capture
    :param instructions: the instructions of a reset's body or a
        shift's handler, without those of a function defined within it
    :param refusal: the message's text after the form's name
    """

    if any(op in YIELD_OPS for op, _ in instructions):
        raise SpecialError(
            f"{called_by.datum} {refusal}",
            called_by.datum, position=called_by.position)


def helper_handler(
        compiler, called_by, body, fragment, tag_count, k=None):
    """
    Emit the row that aborts to a prompt and the handler it runs into
    fragment: an ABORT row, or with k a SHIFT_VAR row, which captures
    the continuation and binds it to K first. The handler is compiled
    in the scope the form is written in, so K and every other name in
    it are that scope's, and is laid out after the row: the row starts
    a frame over this code at the handler's first instruction and the
    handler ends in a RETURN. A SHIFT_VAR row's label follows it, where
    the frame the shift is written in goes on when the continuation is
    called; an ABORT has none, since nothing comes back to it. A yield written in the handler is refused, since the
    handler's frame is not the generator's; one in a function defined
    within it belongs to that function

    :param compiler: the Compiler
    :param called_by: the special's called-by capture
    :param body: the BODY capture
    :param fragment: the fragment to emit into
    :param tag_count: 0 for the default tag, 1 for a tag the fragment
        has already pushed
    :param k: the K capture of a shift, or None for an abort
    """

    inner = compiler.fragment("<handler>")
    if k is not None:
        inner.declare_var(k.datum, position=k.position)
    inner = helper_begin(inner, body.datum)
    refuse_yield(called_by, inner.instructions, "handler cannot yield")

    if k is None:
        # nothing comes back to an abort: what follows the handler in
        # the enclosing form is never reached
        fragment.instruction(OpCode.ABORT, tag_count)
        fragment.merge(inner)
        fragment.instruction(OpCode.RETURN, 1)
        return

    after = compiler.gen_label("shift.end")
    fragment.instruction(OpCode.SHIFT_VAR, (tag_count, after, k.datum, 0))
    fragment.merge(inner)
    fragment.instruction(OpCode.RETURN, 1)
    fragment.instruction(OpCode.LABEL, after)


def helper_reset_body(compiler, called_by, body, fragment, tag_count):
    """
    Emit a RESET row and the body it runs into fragment. The body is
    compiled in the scope the reset is written in, so its names are
    that scope's, and is laid out after the row: the RESET starts a
    frame over this code at the body's first instruction, the body
    ends in a RETURN, and the RESET's label is where the frame the
    reset is written in goes on. A yield written in the body is
    refused, since the body's frame is not the generator's; one in a
    function defined within it belongs to that function

    :param compiler: the Compiler
    :param called_by: the special's called-by capture
    :param body: the BODY capture
    :param fragment: the fragment to emit into
    :param tag_count: 0 for the default tag, 1 for a tag the fragment
        has already pushed
    """

    inner = helper_begin(compiler.fragment("<reset>"), body.datum)
    refuse_yield(called_by, inner.instructions, "body cannot yield")

    after = compiler.gen_label("reset.end")
    fragment.instruction(OpCode.RESET, (tag_count, after, 0, 0))
    fragment.merge(inner)
    fragment.instruction(OpCode.RETURN, 1)
    fragment.instruction(OpCode.LABEL, after)


@builtin("compile/shift")
@special("compile/shift", patterns=_K_BODY_PATTERNS, unpack="kwargs")
def special_shift(compiler, called_by, k, body):
    """
    Compile (compile/shift K EXPR...). Captures the continuation up to
    the nearest prompt of the default tag, aborts to that prompt, and
    runs the handler EXPR... there in a frame of its own, with K bound
    to the continuation in the scope the shift is written in. The
    handler's value is the reset's. K must be a symbol
    """

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    helper_handler(compiler, called_by, body, fragment, 0, k)

    return fragment


@builtin("compile/reset-at")
@special("compile/reset-at", patterns=_TAG_BODY_PATTERNS, unpack="kwargs")
def special_reset_at(compiler, called_by, tag, body):
    """
    Compile (compile/reset-at TAG EXPR...). Evaluates TAG, then runs
    the body in a frame of its own above a prompt carrying TAG. A
    compile/shift-at seeking the same tag, by identity, captures up to
    this prompt. A TAG of None is the default tag, the one
    compile/reset uses
    """

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    fragment.merge(compiler.full_compile(tag.datum))
    helper_reset_body(compiler, called_by, body, fragment, 1)

    return fragment


@builtin("compile/shift-at")
@special("compile/shift-at", patterns=_TAG_K_BODY_PATTERNS, unpack="kwargs")
def special_shift_at(compiler, called_by, tag, k, body):
    """
    Compile (compile/shift-at TAG K EXPR...). Evaluates TAG, captures the
    continuation up to the nearest prompt carrying it, aborts to that
    prompt, and runs the handler EXPR... there in a frame of its own,
    with K bound to the continuation in the scope the shift is written
    in. K must be a symbol. Prompts of other tags between are crossed
    and become part of the continuation
    """

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    fragment.merge(compiler.full_compile(tag.datum))
    helper_handler(compiler, called_by, body, fragment, 1, k)

    return fragment


@builtin("compile/abort")
@special("compile/abort", patterns=_BODY_PATTERNS, unpack="kwargs")
def special_abort(compiler, called_by, body):
    """
    Compile (compile/abort EXPR...). Aborts to the nearest prompt of
    the default tag and runs the handler EXPR... there in a frame of
    its own. The handler's value is the reset's. No continuation is
    captured
    """

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    helper_handler(compiler, called_by, body, fragment, 0)

    return fragment


@builtin("compile/abort-at")
@special("compile/abort-at", patterns=_TAG_BODY_PATTERNS, unpack="kwargs")
def special_abort_at(compiler, called_by, tag, body):
    """
    Compile (compile/abort-at TAG EXPR...). Evaluates TAG, aborts to
    the nearest prompt carrying it, and runs the handler EXPR... there
    in a frame of its own. The handler's value is the reset-at's. No
    continuation is captured
    """

    fragment = compiler.fragment(called_by.datum)
    fragment.position(called_by.position)
    fragment.merge(compiler.full_compile(tag.datum))
    helper_handler(compiler, called_by, body, fragment, 1)

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
        if is_keyword(rest.car):
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
    Compile (compile/await) or (compile/await EXPR). Zero-arg: bare AWAIT
    (0). One-arg: evaluate EXPR, then AWAIT (1) on TOS.
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
