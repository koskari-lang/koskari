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
koskari.compile.specials.imports

Special form handlers for compile/import, compile/import-from, and
compile/import-all-from.

Each import special compiles to a single self-contained opcode plus the
CONST None statement value. The opcodes carry the module name and
relative-import level in their EXTRA rows, import the module themselves,
and store their own bindings; nothing touches the operand stack.

Opcode sequences produced
==========================
(import foo)
    IMPORT ("foo", 0, root, "foo") / CONST None

(import foo.bar)
    IMPORT ("foo.bar", 0, root, "foo") / CONST None      (root binding)

(import [foo.bar as: fbar])
    IMPORT ("foo.bar", 0, leaf, "fbar") / CONST None     (leaf binding)

(import .foo)
    IMPORT ("foo", 1, leaf, "foo") / CONST None

(import-from foo bar [baz as: bz])
    IMPORT_FROM ("foo", 0, ("bar", "baz"), ("bar", "bz")) / CONST None

(import-all-from foo)
    IMPORT_STAR ("foo", 0, 0, 0) / CONST None

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...exceptions import SpecialError
from ...lib import OpCode
from ...matcher import CALLEDBY, MATCH, REST
from ...bootstrap import builtin
from ...syntax import special
from ...types import is_nil, is_keyword, is_pair, is_symbol, unpack
from ..helpers import helper_using, is_symbolish


__all__ = (
    'special_import',
    'special_import_all_from',
    'special_import_from',
)


_IMPORT_ROOT = 0
_IMPORT_LEAF = 1


_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


def _attr_form_to_dotted(form, source):
    """
    Convert (attr obj name) or nested equivalent to dotted string.

    :param form: (attr obj name) with obj symbol or nested attr
    :param source: source form for error context
    :return: str like "os.path" or "foo.bar.baz"
    :raises SpecialError: when form is invalid
    """

    if is_symbol(form) or isinstance(form, str):
        return str(form)

    if not is_pair(form):
        raise SpecialError(
            "import argument must be symbol or (attr ...)",
            source)

    head, tail = form
    if str(head) != 'attr':
        raise SpecialError(
            "import argument must be symbol or (attr ...)",
            source)

    parts = list(unpack(tail))
    if len(parts) < 2:
        raise SpecialError(
            "attr in import requires two arguments",
            source)

    result = []
    for p in parts:
        if is_symbol(p) or isinstance(p, str):
            result.append(str(p))
        elif is_pair(p) and str(p[0]) == 'attr':
            result.append(_attr_form_to_dotted(p, source))
        else:
            raise SpecialError(
                "import attr arguments must be symbols",
                source)

    return '.'.join(result)


def _parse_import_name(name_form, source):
    """
    Split a module name (symbol or (attr ...)) into (level:int,
    dotted_name:str).

    Counts leading dots for relative imports and strips them from the
    returned name.  A bare dot sequence (level > 0, no name) returns
    (level, None) representing an import of the enclosing package itself.

    :param name_form: symbol like 'foo', 'foo.bar', '.foo' or (attr
        obj name) from reader expansion
    :param source: source form for error context
    :return: (level, name) where level >= 0 and name is str or None
    :raises SpecialError: when name_form is not a symbol, str, or attr form
    """

    if is_symbol(name_form) or isinstance(name_form, str):
        raw = str(name_form)
        stripped = raw.lstrip('.')
        level = len(raw) - len(stripped)
        name = stripped if stripped else None
        return (level, name)

    if is_pair(name_form) and str(name_form[0]) == 'attr':
        dotted = _attr_form_to_dotted(name_form, source)
        return (0, dotted)

    raise SpecialError(
        "import argument must be a symbol or (attr ...)",
        source)


def _leaf_name(dotted):
    """
    Return the last component of a dotted module name, or the name itself.

    :param dotted: str such as "foo.bar.baz" or "foo"
    :return: str "baz" or "foo"
    """

    return dotted.rsplit('.', 1)[-1]


def _root_name(dotted):
    """
    Return the first component of a dotted module name, or the name itself.

    :param dotted: str such as "foo.bar.baz" or "foo"
    :return: str "foo"
    """

    return dotted.split('.', 1)[0]


def _parse_member(elem, source, what):
    """
    Parse one bracketed or plain member element.

    Accepted forms:
        SYM               -> no explicit binding, target SYM
        [TARGET as: BIND] -> bind as BIND, target TARGET

    The target is returned as the raw form (symbol or attr form) so
    callers can apply their own name rules to it.

    :param elem: one member element
    :param source: source form for error context
    :param what: form name for error messages
    :return: (bind_name:str or None, target_form)
    :raises SpecialError: on invalid member form
    """

    if is_symbolish(elem):
        return (None, elem)

    if is_pair(elem):
        try:
            name, value = helper_using(elem)
        except SpecialError:
            raise SpecialError(
                f"{what} member must be a symbol or [TARGET as: BIND],"
                f" not {elem!r}",
                source) from None

        if name is None:
            return (None, value)

        if not is_symbolish(name):
            raise SpecialError(
                f"{what} member binding must be a symbol, not {name!r}",
                source)
        return (str(name), value)

    raise SpecialError(
        f"{what} member must be a symbol or [TARGET as: BIND], not"
        f" {elem!r}",
        source)


@builtin("compile/import")
@special("compile/import", patterns=_DEFAULT_PATTERNS)
def special_import(compiler, source, tc=False):
    """
    Compile (import MODULE) and (import [MODULE as: BIND]).

    Plain dotted import binds the root package, Python-style:
    (import foo.bar) binds foo, with foo.bar reachable as an attribute.
    The bracketed member form binds BIND to the leaf module. Relative
    imports (leading dots) are always leaf-style.
    """

    called_by, rest = source
    if is_nil(rest):
        raise SpecialError("import requires a module name", source)

    arg, rest = rest

    if not is_nil(rest):
        maybe_as, _ = rest
        if is_keyword(maybe_as) and str(maybe_as) == 'as':
            raise SpecialError(
                "inline as: is not supported; use"
                " (import [MODULE as: BIND])",
                source)
        raise SpecialError("too many arguments to import", source)

    bind = None
    name_form = arg
    if is_pair(arg) and str(arg[0]) != 'attr':
        bind, name_form = _parse_member(arg, source, "import")

    level, dotted = _parse_import_name(name_form, source)

    if bind is not None:
        leaf = _IMPORT_LEAF
    elif level > 0:
        # relative imports are always leaf-style
        if dotted is None:
            raise SpecialError(
                "bare relative import requires the [MODULE as: BIND]"
                " member form",
                source)
        leaf = _IMPORT_LEAF
        bind = _leaf_name(dotted)
    else:
        if dotted is None:
            raise SpecialError("import requires a module name", source)
        leaf = _IMPORT_ROOT
        bind = _root_name(dotted)

    frag = compiler.fragment(called_by)
    frag.declare_var(bind)
    frag.instruction(
        OpCode.IMPORT, (dotted, level, leaf, bind))
    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/import-from")
@special("compile/import-from", patterns=_DEFAULT_PATTERNS)
def special_import_from(compiler, source, tc=False):
    """
    Compile (import-from NAME MEMBER...) where each MEMBER is either a
    plain symbol or a [ATTR as: BIND] rename form.

    NAME is a symbol; leading dots indicate relative imports.  At least
    one member is required.  Emits a single IMPORT_FROM instruction
    carrying the module name, level, and parallel attr/bind name tuples.
    """

    called_by, rest = source
    if is_nil(rest):
        raise SpecialError("import-from requires a module name", source)

    name_sym, rest = rest
    if is_nil(rest):
        raise SpecialError("import-from requires at least one member", source)

    level, dotted = _parse_import_name(name_sym, source)

    attrs = []
    binds = []
    current = rest
    while not is_nil(current):
        elem, current = current
        bind, target = _parse_member(elem, source, "import-from")
        if not is_symbolish(target):
            raise SpecialError(
                f"import-from member must name an attribute, not"
                f" {target!r}",
                source)
        attr = str(target)
        attrs.append(attr)
        binds.append(bind if bind is not None else attr)

    frag = compiler.fragment(called_by)
    for bind in binds:
        frag.declare_var(bind)

    attrs = tuple(attrs)
    binds = tuple(binds)
    if attrs == binds:
        # share one tuple constant when there are no renames
        binds = attrs

    frag.instruction(
        OpCode.IMPORT_FROM, (dotted, level, attrs, binds))
    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/import-all-from")
@special("compile/import-all-from", patterns=_DEFAULT_PATTERNS)
def special_import_all_from(compiler, source, tc=False):
    """
    Compile (import-all-from MODULE): Python's `from MODULE import *`.

    Binds the module's public names (__all__ when defined, otherwise the
    non-underscore dir() names) into the module namespace.  Only allowed
    at module top level, matching Python.
    """

    called_by, rest = source
    if is_nil(rest):
        raise SpecialError("import-all-from requires a module name", source)

    name_sym, rest = rest
    if not is_nil(rest):
        raise SpecialError("too many arguments to import-all-from", source)

    if compiler.scope_depth > 0:
        raise SpecialError(
            "import-all-from is only allowed at module level", source)

    level, dotted = _parse_import_name(name_sym, source)

    frag = compiler.fragment(called_by)
    frag.instruction(
        OpCode.IMPORT_STAR, (dotted, level, 0, 0))
    frag.instruction(OpCode.CONST, None)

    return frag


# The end.
