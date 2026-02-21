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

Special form handlers for compile/reset and compile/shift.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, symbol
from ...types import is_pair
from ..exceptions import CompilerException
from ..helpers import helper_begin, is_symbolish, special


@special(symbol("compile/reset"))
def special_reset(compiler, source, tc=False):
    """
    Compile (compile/reset EXPR...). Establishes a prompt (tag 0), runs body,
    pops prompt. Enables delimited continuations via compile/shift.
    """

    called_by, rest = source
    if rest is nil or not is_pair(rest):
        raise CompilerException("reset requires body", source)

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
        raise CompilerException(
            "shift requires continuation variable and body",
            source)

    k, body = rest
    if not is_symbolish(k):
        raise CompilerException(
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
