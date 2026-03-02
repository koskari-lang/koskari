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

Special form handlers for compile/ref, compile/attr, compile/del-attr,
compile/set-attr, and compile/ref-attr.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import cons, nil, OpCode, symbol
from ...types import is_pair, is_symbol
from ...exceptions import SpecialError
from ..helpers import is_symbolish, special


_sym_attr = symbol("attr")
_sym_compile_attr = symbol("compile/attr")
_sym_compile_ref_attr = symbol("compile/ref-attr")
_sym_compile_ref_item = symbol("compile/ref-item")
_sym_compile_refq = symbol("compile/refq")
_sym_item = symbol("item")
_sym_compile_item = symbol("compile/item")


@special(symbol("compile/ref"), aliases=(symbol("ref"),))
def special_ref(compiler, source, tc=False):
    """
    Compile (ref ARG) or (compile/ref ARG). Selective expansion:
    - sym -> (compile/refq sym)
    - (attr obj name) or (compile/attr obj name) -> (compile/ref-attr obj name)
    - (item coll key) or (compile/item coll key) -> (compile/ref-item coll key)
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("ref requires one argument", source)

    arg, rest = body
    if rest is not nil:
        raise SpecialError("ref takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(body)

    if is_symbol(arg) or is_symbolish(arg):
        expanded = cons(_sym_compile_refq, arg, nil)
    elif is_pair(arg):
        head, tail = arg
        head_str = str(head) if head else ""
        if head_str in ("attr", "compile/attr"):
            if not is_pair(tail):
                raise SpecialError("ref attr form requires two arguments", source)
            obj, (name, name_rest) = tail
            if name_rest is not nil:
                raise SpecialError("ref attr form takes two arguments", source)
            expanded = cons(_sym_compile_ref_attr, obj, cons(name, nil))
        elif head_str in ("item", "compile/item"):
            if not is_pair(tail):
                raise SpecialError("ref item form requires two arguments", source)
            coll, (key, key_rest) = tail
            if key_rest is not nil:
                raise SpecialError("ref item form takes two arguments", source)
            expanded = cons(_sym_compile_ref_item, coll, cons(key, nil))
        else:
            raise SpecialError(
                "ref argument must be symbol, (attr obj name), or (item coll key)",
                source)
    else:
        raise SpecialError(
            "ref argument must be symbol, (attr obj name), or (item coll key)",
            source)

    if hasattr(source, "set_position") and hasattr(expanded, "set_position"):
        expanded.set_position(source.get_position())
    return compiler.compile_pair(frag, expanded, tc)


@special(symbol("compile/attr"))
def special_attr(compiler, source, tc=False):
    """
    Compile (compile/attr OBJ SYM). Load attribute SYM from OBJ
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("attr requires two arguments", source)
    obj, (sym, rest) = body

    if rest is not nil:
        raise SpecialError("attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR, str(sym))

    return frag


@special(symbol("compile/del-attr"))
def special_del_attr(compiler, source, tc=False):
    """
    Compile (compile/del-attr OBJ SYM). Delete attribute SYM from OBJ
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("del-attr requires two arguments", source)
    obj, (sym, rest) = body

    if rest is not nil:
        raise SpecialError("del-attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR_CLEAR, str(sym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/set-attr"))
def special_set_attr(compiler, source, tc=False):
    """
    Compile (compile/set-attr OBJ SYM VAL). Store VAL into attribute SYM on OBJ
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("set-attr requires three arguments", source)
    obj, (sym, (val, rest)) = body

    if rest is not nil:
        raise SpecialError("set-attr takes three arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.merge(compiler.full_compile(val))
    frag.instruction(OpCode.KOS_OP_ATTR_STORE, str(sym))
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@special(symbol("compile/deref"))
def special_deref(compiler, source, tc=False):
    """
    Compile (compile/deref EXPR). Compile EXPR (produces ref on stack),
    then emit DEREF to call ref.deref() and push result.
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("deref requires one argument", source)

    arg, rest = body
    if rest is not nil:
        raise SpecialError("deref takes one argument", source)

    frag = compiler.fragment(called_by)
    frag.position_of(body)
    frag.merge(compiler.full_compile(arg))
    frag.instruction(OpCode.KOS_OP_DEREF, 0)

    return frag


@special(symbol("compile/ref-attr"))
def special_ref_attr(compiler, source, tc=False):
    """
    Compile (compile/ref-attr OBJ SYM). Produce a mutable reference to attribute SYM on OBJ
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("ref-attr requires two arguments", source)
    obj, (sym, rest) = body

    if rest is not nil:
        raise SpecialError("ref-attr takes two arguments", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.merge(compiler.full_compile(obj))
    frag.instruction(OpCode.KOS_OP_ATTR_REF, str(sym))

    return frag


# The end.
