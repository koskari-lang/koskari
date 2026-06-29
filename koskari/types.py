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

from .lib import KoskariSyncViolation, cons, keyword, nil, nilp, pair, symbol


__all__ = (
    "TypePredicate",
    "BuiltinPredicate",
    "is_nil",
    "is_None",
    "is_proper",
    "is_recursive",
    "build_proper",
    "unpack",
    "is_on_demand",
)


class TypePredicate(partial):
    """
    A predicate function that tests whether an object is an instance
    of a particular type.
    """

    def __new__(cls, name, typeobj):
        obj = partial.__new__(cls, typeobj.__instancecheck__)
        obj.__name__ = name or (typeobj.__name__ + "?")
        return obj


    def __repr__(self):
        return "<builtin type predicate %s>" % self.__name__


class BuiltinPredicate(partial):
    """
    A predicate function that tests an object using a callable.
    """

    def __new__(cls, name, call, *args, **kwds):
        check = partial.__new__(cls, call, *args, **kwds)
        check.__name__ = name
        return check


    def __repr__(self):
        return "<builtin predicate %s>" % self.__name__


is_symbol = TypePredicate("symbol?", symbol)
is_keyword = TypePredicate("keyword?", keyword)
is_pair = TypePredicate("pair?", pair)

is_nil = BuiltinPredicate("nil?", nilp)
is_None = BuiltinPredicate("None?", operator.is_, None)


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
    do not already have a position.

    :param value: a pair to fill position on
    :param position: the position tuple to set
    :param follow: if True, fill position on all pairs in the tail
    """

    if position and is_pair(value):
        value.fill_position(position, follow)


# The end.
