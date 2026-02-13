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

    # bytecode types
    Code,
    OpCode,
    OpStyle,

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

    # bytecode types
    "Code",
    "OpCode",
    "OpStyle",

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

    # bytecode fragment
    "unique_append",
    "ByteCodeFragment",

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


def unique_append(lst, val):
    """
    Append val to lst only if it is not already present

    :param lst: a mutable sequence
    :param val: value to append
    """

    if val not in lst:
        lst.append(val)


class ByteCodeFragment:
    """
    Holds a fragment of bytecode during compilation: instructions plus
    closure requests and name declarations for the namespace
    """

    def __init__(self, name=None, filename=None, mode=None, compiler=None):
        """
        :param name: optional fragment name (e.g. for a block or loop)
        :param filename: optional source filename
        :param mode: optional compilation mode
        :param compiler: optional compiler/builder used to complete this fragment
        """

        self.name = name
        self.filename = filename
        self.mode = mode
        self.compiler = compiler
        self.instructions = []
        self.requests = []
        self.declares = []


    def as_namespace_spec(self):
        """
        Build a NamespaceSpec from this fragment's name and declarations.
        Local names are declares; closure names are requests.

        :return: NamespaceSpec(name, declares, requests)
        """

        return NamespaceSpec(
            self.name,
            tuple(self.declares),
            tuple(self.requests),
        )


    def request_var(self, var_name):
        """
        Record a closure request for var_name unless it is already declared

        :param var_name: name to request from enclosing scope
        """

        var_name = str(var_name)
        if var_name not in self.declares:
            unique_append(self.requests, var_name)


    def declare_var(self, var_name):
        """
        Record a name declared in this fragment's namespace

        :param var_name: name to declare
        """

        unique_append(self.declares, str(var_name))


    def declare_vars(self, var_names):
        """
        Record multiple names declared in this fragment's namespace

        :param var_names: iterable of names to declare
        """

        for var_name in var_names:
            unique_append(self.declares, str(var_name))


    def position_of(self, source):
        """
        Emit a position instruction from the source's position if set

        :param source: a pair or object with get_position
        """

        pos = get_position(source)
        if pos:
            self.position(pos)


    def position(self, linecol, extra=None):
        """
        Emit a position instruction

        :param linecol: (line, column) or position tuple
        :param extra: optional extra position data (e.g. source text)
        """

        line, col = linecol[:2]
        self.instruction(OpCode.KOS_OP_POSITION, (line, col, extra))


    def instruction(self, op, opa):
        """
        Append one bytecode instruction

        :param op: OpCode for the instruction
        :param opa: argument for the instruction
        """

        self.instructions.append((op, opa))


    def merge(self, other):
        """
        Merge another fragment into this one: adopt its declarations and
        requests, then append its instructions

        :param other: ByteCodeFragment to merge in
        """

        for name in other.declares:
            self.declare_var(name)
        for name in other.requests:
            self.request_var(name)
        self.instructions.extend(other.instructions)


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
