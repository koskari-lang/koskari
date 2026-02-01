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

Koskari native types, predicates, and helper functions.

This module re-exports the native types and functions from the C
extension library and provides Python-level type predicates and
utility functions.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from functools import partial
import operator

from .lib import (
    # atom types
    symbol,
    keyword,

    # pair types
    pair,
    nil,

    # namespace types
    Namespace,
    NamespaceSpec,
    CollectorNamespace,
    ModuleNamespace,
    Module,

    # formals types
    Formals,
    FormalsSpec,

    # values type
    values,

    # parse types
    BaseSourceStream,

    # atom functions
    gensym,

    # pair functions
    cons,
    car,
    cdr,
    setcar,
    setcdr,
    build_unpack_pair,
)


__all__ = (
    # atom types
    "symbol",
    "keyword",

    # pair types
    "pair",
    "nil",

    # namespace types
    "Namespace",
    "NamespaceSpec",
    "CollectorNamespace",
    "ModuleNamespace",
    "Module",

    # formals types
    "Formals",
    "FormalsSpec",

    # values type
    "values",

    # parse types
    "BaseSourceStream",

    # atom functions
    "gensym",

    # pair functions
    "cons",
    "car",
    "cdr",
    "setcar",
    "setcdr",
    "build_unpack_pair",

    # type predicates
    "is_symbol",
    "is_keyword",
    "is_pair",
    "is_nil",
    "is_proper",
    "is_recursive",

    # predicate helpers
    "TypePredicate",
    "BuiltinPredicate",

    # pair helpers
    "build_proper",
    "unpack",

    # position helpers
    "get_position",
    "set_position",
    "fill_position",

    # exceptions
    "KoskariException",
    "NotYetImplemented",
    "KoskariSyntaxError",
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
is_nil = BuiltinPredicate("nil?", operator.is_, nil)


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


def get_position(value, default=None):
    """
    Get the source position associated with a pair.

    :param value: a pair or other object
    :param default: value to return if no position is set
    :return: the position tuple, or default
    """

    return (value.get_position() or default) if is_pair(value) else default


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


class KoskariException(Exception):
    """
    Base class for error-driven Exceptions raised by Koskari.
    """

    pass


class NotYetImplemented(KoskariException):
    """
    Raised as a placeholder for features that have not been
    implemented yet.
    """

    pass


class KoskariSyntaxError(SyntaxError):
    """
    An error in Koskari syntax, either during read or compile time.
    """

    def __init__(self, message, location=None, filename=None, text=None):

        if filename:
            if not location:
                location = (1, 0)
            super().__init__(message, (filename, *location, text))
            self.print_file_and_line = True
        else:
            super().__init__(message)

        self.message = message
        self.location = location
        self.filename = filename
        self.text = text


# The end.
