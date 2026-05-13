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
koskari.compile.specials.refs

Special form handlers for refs: compile/deref, compile/ref-attr,
compile/ref-item, compile/refq, and compile/bindq.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...lib import nil, OpCode
from ...matcher import DEFINE, MATCH, REST, SYMBOL, VALUE
from ...types import is_pair
from ...exceptions import SpecialError
from ...stdlib import builtin
from ...syntax import special
from ..helpers import const_only_expr


_DEFAULT_PATTERNS = (MATCH(SYMBOL("called-by"), REST("rest")),)


@builtin("compile/deref")
@special("compile/deref", patterns=_DEFAULT_PATTERNS)
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


@builtin("compile/ref-attr")
@special("compile/ref-attr", patterns=_DEFAULT_PATTERNS)
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


@builtin("compile/ref-item")
@special("compile/ref-item", patterns=_DEFAULT_PATTERNS)
def special_ref_item(compiler, source, tc=False):
    """
    Compile (compile/ref-item OBJ KEY). Produce a mutable reference to item at KEY in OBJ
    """

    called_by, body = source
    if not is_pair(body):
        raise SpecialError("ref-item requires two arguments", source)
    obj, (key, rest) = body

    if rest is not nil:
        raise SpecialError("ref-item takes two arguments", source)

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


@builtin("compile/refq")
@special("compile/refq", patterns=_DEFAULT_PATTERNS)
def special_refq(compiler, source, tc=False):
    """
    Compile (compile/refq SYM). Load the value of the binding named SYM
    """

    called_by, body = source
    sym, rest = body

    sym = str(sym)

    if rest is not nil:
        raise SpecialError("refq takes one argument", source)

    frag = compiler.fragment(called_by)

    frag.position_of(body)
    frag.request_var(sym)
    frag.instruction(OpCode.KOS_OP_VAR_REF, sym)

    return frag


_BINDQ_PATTERNS = [
    MATCH(SYMBOL("_"), DEFINE("name"), VALUE("value")),
]


@builtin("compile/bindq")
@special("compile/bindq", patterns=_BINDQ_PATTERNS, unpack="match")
def special_bindq(compiler, caps, tc=False):
    """
    Compile (compile/bindq SYM EXPR). Declare a new binding SYM and initialize
    it to EXPR
    """

    called_by = caps["_"].datum
    pos = caps["_"].position

    namesym = caps["name"].datum
    name = str(namesym)

    value = caps["value"]
    value_datum = value.datum

    frag = compiler.fragment(called_by)
    frag.declare_var(name)

    if not is_pair(value_datum):
        frag.position(value.position)
    frag.merge(compiler.full_compile(value_datum))

    frag.position(pos)
    frag.instruction(OpCode.KOS_OP_VAR_BIND, name)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


# The end.
