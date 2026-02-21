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
koskari.compile.specials.slots

Special form handlers for compile/attr, compile/del-attr, compile/set-attr,
and compile/ref-attr.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, symbol
from ..exceptions import CompilerException
from ..helpers import special


@special(symbol("compile/attr"))
def special_attr(compiler, source, tc=False):
    called_by, body = source
    obj, (sym, rest) = body

    if rest is not nil:
        raise CompilerException("attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR, str(sym))

    return frag


@special(symbol("compile/del-attr"))
def special_del_attr(compiler, source, tc=False):
    called_by, body = source
    obj, (sym, rest) = body

    if rest is not nil:
        raise CompilerException("del-attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR_CLEAR, str(sym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/set-attr"))
def special_set_attr(compiler, source, tc=False):
    called_by, body = source
    obj, (sym, (val, rest)) = body

    if rest is not nil:
        raise CompilerException("set-attr takes three arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.merge(compiler.full_compile(val))
    frag.instruction(OpCode.KOS_OP_ATTR_STORE, str(sym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/ref-attr"))
def special_ref_attr(compiler, source, tc=False):
    called_by, body = source
    obj, (sym, rest) = body

    if rest is not nil:
        raise CompilerException("ref-attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR_REF, str(sym))

    return frag


# The end.
