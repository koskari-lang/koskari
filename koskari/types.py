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
koskari.types

Koskari native types, predicates, and helper functions

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import operator
from functools import partial

from .lib import (
    FunctionSpec, KoskariSyncViolation, NamespaceSpec, Parameterize,
    bracket_nil, bracket_pair, cons, gensym, keyword, nil, nilp, pair,
    symbol)


__all__ = (
    "TypePredicate",
    "BuiltinPredicate",
    "is_nil",
    "is_None",
    "is_const",
    "is_proper",
    "is_recursive",
    "build_proper",
    "bracket_splice",
    "parameterize",
    "unpack",
    "is_on_demand",
    "strip_mark",
    "strip_marks",
)


def parameterize(mapping=None, /, **bindings):
    """
    Bind parameters for the extent of a Python block.

    The Python-side counterpart of the koskari parameterize special.
    Inside the block a read of each named parameter, from Python or
    from koskari code called within it, sees the bound value; at exit
    the prior binding answers again. A koskari context, coroutine or
    asyncio task created inside the block keeps the bindings applied
    at its creation. Blocks belong to the running stack: a thread's
    synchronous stack, or one asyncio task.

    :param mapping: parameter names (str or symbol) to values, for
        names that are not Python identifiers
    :param bindings: parameter names to values
    :return: the context manager
    """

    if mapping:
        bindings = {**mapping, **bindings}
    return Parameterize(bindings)


class TypePredicate(partial):
    """
    A predicate function that tests whether an object is an instance
    of a particular type.
    """

    def __new__(cls, name, typeobj):
        # bound through the metaclass, as isinstance binds it: for type
        # itself the attribute lookup finds the descriptor in type's
        # own dict and hands it back unbound
        meta = type(typeobj)
        check = meta.__instancecheck__.__get__(typeobj, meta)
        obj = partial.__new__(cls, check)
        obj.__name__ = name or (typeobj.__name__ + "?")
        return obj


    def __repr__(self):
        return f"<builtin type predicate {self.__name__}>"


class BuiltinPredicate(partial):
    """
    A predicate function that tests an object using a callable.
    """

    def __new__(cls, name, call, *args, **kwds):
        check = partial.__new__(cls, call, *args, **kwds)
        check.__name__ = name
        return check


    def __repr__(self):
        return f"<builtin predicate {self.__name__}>"


is_symbol = TypePredicate("symbol?", symbol)
is_keyword = TypePredicate("keyword?", keyword)
is_pair = TypePredicate("pair?", pair)

is_nil = BuiltinPredicate("nil?", nilp)
is_None = BuiltinPredicate("None?", operator.is_, None)


def strip_mark(value):
    """
    The base symbol of a gensym, or value unchanged. Use where an atom
    is read as a tag, a label, or a literal: hygiene renames only the
    names a form binds and resolves
    """

    return value.base if type(value) is gensym else value


def strip_marks(form):
    """
    A copy of a form with every gensym replaced by its base symbol.
    Pair cells are rebuilt with their positions and delimiter style;
    atoms other than gensyms are returned as they are
    """

    if type(form) is gensym:
        return form.base

    if not isinstance(form, pair) or nilp(form):
        return form

    cells = []
    tail = form
    while isinstance(tail, pair) and not nilp(tail):
        cells.append(tail)
        tail = tail.cdr

    out = strip_marks(tail)
    for cell in reversed(cells):
        car = strip_marks(cell.car)
        if isinstance(cell, bracket_pair):
            out = bracket_pair(car, out)
        else:
            out = cons(car, out)
        pos = get_position(cell)
        if pos:
            out.set_position(pos)
    return out


CONST_TYPES = (
    str, bytes,
    bool, int, float, complex,
    slice,
    type(None), type(...),
    symbol, gensym, keyword, type(nil), type(bracket_nil),
    FunctionSpec,
    NamespaceSpec,
)

CONST_CONTAINER_TYPES = (tuple, frozenset)


def is_const(value):
    """
    Returns True if value is constant-pool material: exactly one of the
    CONST_TYPES, or exactly a tuple or frozenset of such values. A
    subclass is refused, since the pool keeps only its builtin kind
    """

    kind = type(value)
    if kind in CONST_TYPES:
        return True
    return (kind in CONST_CONTAINER_TYPES
            and all(is_const(x) for x in value))


def is_proper(value):
    """
    Returns True if value is a proper list (a pair terminated by nil).
    """

    return is_pair(value) and value.is_proper()


def is_recursive(value):
    """
    Returns True if value is a recursive (circular) pair structure.
    """

    return is_pair(value) and value.is_recursive()


def build_proper(*values):
    """
    Create a proper list from the given values.

    :param values: the values to include in the list
    :return: a proper list, or nil if no values given
    """

    return cons(*values, nil) if values else nil


def bracket_splice(*segments):
    """
    The bracket analogue of the PAIR_SPLICE opcode: flatten the given
    segments (pairs, tuples, nil, or other iterables) into a single list
    whose *leading* cell is a bracket-pair. Only the head needs to be a
    bracket cell — repr is governed by the leading cell — so the tail
    stays plain pairs and no full-spine copy is made. An empty result is
    bracket-nil.

    :param segments: the splice segments to concatenate
    :return: the spliced bracket list
    """

    elems = []
    for seg in segments:
        if is_nil(seg):
            continue
        if is_pair(seg):
            elems.extend(seg.unpack())
        else:
            elems.extend(seg)

    if not elems:
        return bracket_nil

    return bracket_pair(elems[0], build_proper(*elems[1:]))


def unpack(value):
    """
    Return an iterator over the elements of value.

    If value is a pair, returns its unpack iterator. Otherwise,
    returns iter(value).

    :param value: a pair or other iterable
    :return: an iterator
    """

    return value.unpack() if is_pair(value) else iter(value)


class _DemandProbe:
    """
    Minimal awaitable used by is_on_demand to probe the execution context.
    """

    def __await__(self):
        yield


async def is_on_demand():
    """
    Return True if the current execution context is a demand context.

    Probes by attempting a single suspension. If running under demand_call
    or demand(fn), KoskariSyncViolation is thrown into the coroutine at the
    yield point and caught here, returning True. If running normally (event
    loop or top-level async), the suspension succeeds and the function
    returns False.

    Intended for Python library code that needs to choose a code path based
    on whether it is being called from a demand context.
    """

    try:
        await _DemandProbe()
        return False
    except KoskariSyncViolation:
        return True


def get_position(value, default=None):
    """
    Get the source position associated with a pair.

    :param value: a pair or other object
    :param default: value to return if no position is set
    :return: the position tuple, or default
    """

    return (value.position or default) if is_pair(value) else default


def set_position(value, position, follow=False):
    """
    Set the source position on a pair, optionally following the tail.

    :param value: a pair to set position on
    :param position: the position tuple to set
    :param follow: if True, set position on all pairs in the tail
    """

    if position and is_pair(value):
        value.set_position(position, follow)


def fill_position(value, position, follow=True):
    """
    Fill in the source position on pairs that do not have one set.

    Unlike set_position, this only sets the position on pairs that
    do not already have a position. A pair that has one ends its
    branch: the walk does not enter its car or continue its spine, so
    what the reader positioned stays as it was and what was built
    around it is filled in.

    :param value: a pair to fill position on
    :param position: the position tuple to set
    :param follow: if True, fill position along the tail and into
        cell cars until a positioned pair ends each branch
    """

    if position and is_pair(value):
        value.fill_position(position, follow)


# The end.
