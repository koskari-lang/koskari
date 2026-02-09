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
koskari

A LISP dialect for Python - a playground language for joyful expression.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

from .types import (
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

    # type predicates
    is_symbol,
    is_keyword,
    is_pair,
    is_nil,
    is_proper,
    is_recursive,

    # predicate helpers
    TypePredicate,
    BuiltinPredicate,

    # pair helpers
    build_proper,
    unpack,

    # position helpers
    get_position,
    set_position,
    fill_position,

    # exceptions
    KoskariException,
    NotYetImplemented,
    KoskariSyntaxError,
)

from .parse import (
    # exceptions
    ReaderSyntaxError,
    FormatStringSyntaxError,

    # stream types
    SourceStream,

    # stream factories
    source_open,
    source_str,
    source_stream,

    # reader
    Reader,
    default_reader,
)

from .lib import (
    Expression,
    Function,
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

    # exceptions
    "KoskariException",
    "NotYetImplemented",
    "KoskariSyntaxError",
    "ReaderSyntaxError",
    "FormatStringSyntaxError",

    # stream types
    "SourceStream",

    # stream factories
    "source_open",
    "source_str",
    "source_stream",

    # reader
    "Reader",
    "default_reader",

    # callables
    "Expression",
    "Function",
)


# The end.
