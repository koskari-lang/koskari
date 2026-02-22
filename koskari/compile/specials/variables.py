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
koskari.compile.specials.variables

Special form handlers for compile/setq, compile/refq, compile/bindq, and
compile/delq.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, symbol
from ...types import is_pair
from ..exceptions import CompilerException
from ..helpers import special


@special(symbol("compile/setq"))
def special_setq(compiler, source, tc=False):
    """
    Compile (compile/setq SYM EXPR). Store the value of EXPR into the existing binding SYM
    """

    called_by, (sym, body) = source
    expr, rest = body

    sym = str(sym)

    if rest is not nil:
        raise CompilerException("setq takes two arguments", source)

    frag = compiler.fragment(called_by)

    if not is_pair(expr):
        frag.position_of(body)

    frag.request_var(sym)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_VAR_STORE, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/refq"))
def special_refq(compiler, source, tc=False):
    """
    Compile (compile/refq SYM). Load the value of the binding named SYM
    """

    called_by, body = source
    sym, rest = body

    sym = str(sym)

    if rest is not nil:
        raise CompilerException("refq takes one argument", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.request_var(sym)
    frag.instruction(OpCode.KOS_OP_VAR_REF, sym)

    return frag


@special(symbol("compile/bindq"))
def special_bindq(compiler, source, tc=False):
    """
    Compile (compile/bindq SYM EXPR). Declare a new binding SYM and initialize it to EXPR
    """

    called_by, (sym, body) = source
    expr, rest = body

    sym = str(sym)

    if rest is not nil:
        raise CompilerException("bindq takes two arguments", source)

    frag = compiler.fragment(called_by)

    if not is_pair(expr):
        frag.position_of(body)

    frag.declare_var(sym)
    frag.merge(compiler.full_compile(expr))
    frag.instruction(OpCode.KOS_OP_VAR_BIND, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/delq"))
def special_delq(compiler, source, tc=False):
    """
    Compile (compile/delq SYM). Clear the binding SYM, resetting it to unset
    """

    called_by, body = source
    sym, rest = body

    sym = str(sym)

    if rest is not nil:
        raise CompilerException("delq takes one argument", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.request_var(sym)
    frag.instruction(OpCode.KOS_OP_VAR_CLEAR, sym)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
