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
compile/capture/cc, compile/reset, and compile/shift.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, symbol, cons
from ...types import is_pair, is_nil
from ...exceptions import SpecialError
from ..helpers import helper_begin, is_symbolish, special, pycons


@special(symbol("compile/call/cc"))
def special_call_cc(compiler, source, tc=False):
    """
    Compile (compile/call/cc FN). Captures continuation, invokes FN with the
    continuation, and returns the result.
    """

    called_by, args = source
    if args is nil or not is_pair(args):
        raise SpecialError("call/cc requires a single argument", source)
    expr, rest = args
    if not is_nil(rest):
        raise SpecialError("call/cc requires a single argument", source)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.instruction(OpCode.KOS_OP_PUSH_CC, 0)
    fragment.merge(compiler.full_compile(expr, False))
    fragment.instruction(OpCode.KOS_OP_CAPTURE_CC, 0)
    fragment.instruction(OpCode.KOS_OP_CALL, 1)
    fragment.instruction(OpCode.KOS_OP_POP_CC, 0)

    return fragment


@special(symbol("compile/let/cc"))
def special_let_cc(compiler, source, tc=False):
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

    fragment.instruction(OpCode.KOS_OP_PUSH_CC, 0)
    with fragment.child_namespace(called_by, [], []) as frag:
        frag.declare_var(k_name)
        frag.instruction(OpCode.KOS_OP_CAPTURE_CC, 0)
        frag.instruction(OpCode.KOS_OP_VAR_STORE, k_name)
        helper_begin(frag, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_CC, 0)

    return fragment


@special(symbol("compile/capture/cc"))
def special_capture_cc(compiler, source, tc=False):
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
    fragment.instruction(OpCode.KOS_OP_PUSH_CC, 0)
    fragment.instruction(OpCode.KOS_OP_CAPTURE_CC, 0)
    fragment.instruction(OpCode.KOS_OP_VAR_STORE, k_name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_CC, 0)

    return fragment


@special(symbol("compile/reset"))
def special_reset(compiler, source, tc=False):
    """
    Compile (compile/reset EXPR...). Establishes a prompt (tag 0), runs body,
    pops prompt. Enables delimited continuations via compile/shift.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise SpecialError("reset requires body", source)

    fragment = compiler.fragment(called_by)
    fragment.position_of(source)
    fragment.instruction(OpCode.KOS_OP_PUSH_PROMPT, 0)
    helper_begin(fragment, rest, False)
    fragment.instruction(OpCode.KOS_OP_POP_PROMPT, 0)

    return fragment


@special(symbol("compile/shift"))
def special_shift(compiler, source, tc=False):
    """
    Compile (compile/shift K EXPR...). Captures continuation, binds to K, runs
    handler body. K must be a symbol; tag 0.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
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
    fragment.instruction(OpCode.KOS_OP_PUSH_SHIFT, 0)
    fragment.declare_var(k_name)
    fragment.instruction(OpCode.KOS_OP_VAR_STORE, k_name)
    helper_begin(fragment, body, False)
    fragment.instruction(OpCode.KOS_OP_POP_SHIFT, 0)

    return fragment


# The end.
