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

Special form handlers for compile/item, compile/del-item, and compile/set-item.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...lib import OpCode
from ...matcher import CALLEDBY, MATCH, VALUE
from ...syntax import special
from ...types import is_symbol
from ..helpers import const_only_expr, emit_item_key, var_only_expr


_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key")),
]

_SET_ITEM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("obj"), VALUE("key"), VALUE("val")),
]


def item_fragment(compiler, called_by, obj):
    """
    A fragment for an item special with its object compiled, positioned
    at the object
    """

    frag = compiler.fragment(called_by.datum)
    frag.position(obj.position)
    frag.merge(compiler.full_compile(obj.datum))
    return frag


@builtin("compile/item")
@special("compile/item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_item(compiler, called_by, obj, key):
    """
    Compile (compile/item OBJ KEY). Load item at KEY from OBJ
    """

    frag = item_fragment(compiler, called_by, obj)
    emit_item_key(compiler, frag, key.datum, OpCode.CONST_ITEM, OpCode.ITEM)
    return frag


@builtin("compile/del-item")
@special("compile/del-item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_del_item(compiler, called_by, obj, key):
    """
    Compile (compile/del-item OBJ KEY). Delete item at KEY from OBJ
    """

    frag = item_fragment(compiler, called_by, obj)
    emit_item_key(
        compiler, frag, key.datum, OpCode.CONST_ITEM_CLEAR, OpCode.ITEM_CLEAR)
    frag.instruction(OpCode.CONST, None)
    return frag


@builtin("compile/take-item")
@special("compile/take-item", patterns=_ITEM_PATTERNS, unpack="kwargs")
def special_take_item(compiler, called_by, obj, key):
    """
    Compile (compile/take-item OBJ KEY). Get item at KEY from OBJ, delete it,
    push the value
    """

    frag = item_fragment(compiler, called_by, obj)
    emit_item_key(
        compiler, frag, key.datum, OpCode.CONST_ITEM_TAKE, OpCode.ITEM_TAKE)
    return frag


def emit_var_item_store(compiler, called_by, obj, key, val):
    """
    The fused store for a symbol object with a constant or symbol key:
    the value compiled first, then one VAR_CONST_ITEM_STORE or
    VAR_ITEM_STORE row that resolves the object (and key) by name when
    it runs. Any other key shape answers None and the caller takes the
    stack path
    """

    keyfrag = compiler.full_compile(key.datum)
    is_const, key_val = const_only_expr(keyfrag)
    is_var, key_name = var_only_expr(keyfrag)
    if not (is_const or is_var):
        return None

    name = str(obj.datum)
    frag = compiler.fragment(called_by.datum)
    frag.position(obj.position)
    frag.merge(compiler.full_compile(val.datum))
    frag.request_var(name)
    if is_const:
        frag.instruction(OpCode.VAR_CONST_ITEM_STORE, (name, key_val, 0, 0))
    else:
        # the key is resolved by name when the row runs, so a captured
        # key is requested from the enclosing scope like the object is
        frag.request_var(key_name)
        frag.instruction(OpCode.VAR_ITEM_STORE, (name, key_name, 0, 0))
    frag.instruction(OpCode.CONST, None)
    return frag


@builtin("compile/set-item")
@special("compile/set-item", patterns=_SET_ITEM_PATTERNS, unpack="kwargs")
def special_set_item(compiler, called_by, obj, key, val):
    """
    Compile (compile/set-item OBJ KEY VAL). Store VAL at item KEY in OBJ
    """

    if is_symbol(obj.datum):
        frag = emit_var_item_store(compiler, called_by, obj, key, val)
        if frag is not None:
            return frag

    frag = item_fragment(compiler, called_by, obj)
    emit_item_key(
        compiler, frag, key.datum, OpCode.CONST_ITEM_STORE, OpCode.ITEM_STORE,
        before=lambda: frag.merge(compiler.full_compile(val.datum)))
    frag.instruction(OpCode.CONST, None)
    return frag


# The end.
