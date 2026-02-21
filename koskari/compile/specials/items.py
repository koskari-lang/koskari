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
koskari.compile.specials.items

Special form handlers for compile/item, compile/ref-item, compile/del-item,
and compile/set-item.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode, symbol
from ..exceptions import CompilerException
from ..helpers import const_only_expr, special


@special(symbol("compile/item"))
def special_item(compiler, source, tc=False):
    called_by, body = source
    obj, (key, rest) = body

    if rest is not nil:
        raise CompilerException("item takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.KOS_OP_CONST_ITEM, key_val)

    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.KOS_OP_ITEM, 0)

    return frag


@special(symbol("compile/ref-item"))
def special_ref_item(compiler, source, tc=False):
    called_by, body = source
    obj, (key, rest) = body

    if rest is not nil:
        raise CompilerException("ref-item takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.KOS_OP_CONST_ITEM_REF, key_val)

    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.KOS_OP_ITEM_REF, 0)

    return frag


@special(symbol("compile/del-item"))
def special_del_item(compiler, source, tc=False):
    called_by, body = source
    obj, (key, rest) = body

    if rest is not nil:
        raise CompilerException("del-item takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.instruction(OpCode.KOS_OP_CONST_ITEM_CLEAR, key_val)

    else:
        frag.merge(keyfrag)
        frag.instruction(OpCode.KOS_OP_ITEM_CLEAR, 0)

    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/set-item"))
def special_set_item(compiler, source, tc=False):
    called_by, body = source
    obj, (key, (val, rest)) = body

    if rest is not nil:
        raise CompilerException("set-item takes three arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))

    keyfrag = compiler.full_compile(key)
    is_const, key_val = const_only_expr(keyfrag)

    if is_const:
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.KOS_OP_CONST_ITEM_STORE, key_val)

    else:
        frag.merge(keyfrag)
        frag.merge(compiler.full_compile(val))
        frag.instruction(OpCode.KOS_OP_ITEM_STORE, 0)

    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
