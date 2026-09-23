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


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...lib import OpCode, gensym, symbol
from ...matcher import BODY, CALLEDBY, MATCH, VALUE
from ...syntax import special
from ...types import is_mark, is_pair, is_symbol, strip_mark, unpack
from ..helpers import helper_using, is_symbolish


__all__ = (
    'special_import',
    'special_import_all_from',
    'special_import_from',
)


_IMPORT_ROOT = 0
_IMPORT_LEAF = 1


_MODULE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("module")),
]

_IMPORT_FROM_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("module"), BODY("members")),
]

_USE_PATTERNS = [
    MATCH(CALLEDBY("called_by"), VALUE("module")),
    MATCH(CALLEDBY("called_by"), VALUE("module"), BODY("stems")),
]


def _attr_form_to_dotted(form, source, position=None):
    """
    Convert (attr obj name) or nested equivalent to dotted string.

    :param form: (attr obj name) with obj symbol or nested attr
    :param source: source form for error context
    :param position: source position recorded on errors
    :return: str like "os.path" or "foo.bar.baz"
    :raises SpecialError: when form is invalid
    """

    form = strip_mark(form)
    if is_symbol(form) or isinstance(form, str):
        return str(form)

    if not is_pair(form):
        raise SpecialError(
            "import argument must be symbol or (attr ...)",
            source, position=position)

    head, tail = form
    if str(strip_mark(head)) != 'attr':
        raise SpecialError(
            "import argument must be symbol or (attr ...)",
            source, position=position)

    parts = list(unpack(tail))
    if len(parts) < 2:
        raise SpecialError(
            "attr in import requires two arguments",
            source, position=position)

    result = []
    for p in parts:
        p = strip_mark(p)
        if is_symbol(p) or isinstance(p, str):
            result.append(str(p))
        elif is_pair(p) and str(strip_mark(p[0])) == 'attr':
            result.append(_attr_form_to_dotted(p, source, position))
        else:
            raise SpecialError(
                "import attr arguments must be symbols",
                source, position=position)

    return '.'.join(result)


def _parse_import_name(name_form, source, position=None):
    """
    Split a module name (symbol or (attr ...)) into (level:int,
    dotted_name:str).

    Counts leading dots for relative imports and strips them from the
    returned name.  A bare dot sequence (level > 0, no name) returns
    (level, None) representing an import of the enclosing package itself.

    :param name_form: symbol like 'foo', 'foo.bar', '.foo' or (attr
        obj name) from reader expansion
    :param source: source form for error context
    :param position: source position recorded on errors
    :return: (level, name) where level >= 0 and name is str or None
    :raises SpecialError: when name_form is not a symbol, str, or attr form
    """

    name_form = strip_mark(name_form)
    if is_symbol(name_form) or isinstance(name_form, str):
        raw = str(name_form)
        stripped = raw.lstrip('.')
        level = len(raw) - len(stripped)
        name = stripped if stripped else None
        return (level, name)

    if is_pair(name_form) and str(strip_mark(name_form[0])) == 'attr':
        dotted = _attr_form_to_dotted(name_form, source, position)
        return (0, dotted)

    raise SpecialError(
        "import argument must be a symbol or (attr ...)",
        source, position=position)


def _name_atoms(name_form):
    """
    The root and leaf atoms of a module name form: the symbol itself,
    or the innermost object and the outermost attribute of an
    ``(attr ...)`` chain

    :param name_form: symbol, gensym, or (attr obj name) form
    :return: (root, leaf)
    """

    if not is_pair(name_form):
        return name_form, name_form
    leaf = name_form
    root = name_form
    while is_pair(root):
        parts = list(unpack(root))
        if len(parts) != 3:
            break
        leaf = parts[2] if leaf is name_form else leaf
        root = parts[1]
    return root, leaf


def _bind_spelling(bind, atom):
    """
    The name an import binds: ``bind`` as computed from the dotted
    module name, carrying the marks of the atom it came from, so a
    template's import binds apart from the use site and the template's
    own references reach it

    :param bind: the bare binding name
    :param atom: the source atom the binding is named after
    """

    if is_mark(atom):
        return str(gensym(symbol(bind), marks=atom.marks))
    return bind


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


def _parse_member(elem, source, what, position=None):
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
    :param position: source position recorded on errors
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
                source, position=position) from None

        if name is None:
            return (None, value)

        if not is_symbolish(name):
            raise SpecialError(
                f"{what} member binding must be a symbol, not {name!r}",
                source, position=position)
        return (str(name), value)

    raise SpecialError(
        f"{what} member must be a symbol or [TARGET as: BIND], not"
        f" {elem!r}",
        source, position=position)


@builtin("compile/import")
@special("compile/import", patterns=_MODULE_PATTERNS, unpack="kwargs")
def special_import(compiler, called_by, module):
    """
    Compile (import MODULE) and (import [MODULE as: BIND]).

    Plain dotted import binds the root package, Python-style:
    (import foo.bar) binds foo, with foo.bar reachable as an attribute.
    The bracketed member form binds BIND to the leaf module. Relative
    imports (leading dots) are always leaf-style.
    """

    pos = called_by.position
    arg = module.datum

    bind = None
    name_form = arg
    if is_pair(arg) and str(strip_mark(arg[0])) != 'attr':
        bind, name_form = _parse_member(arg, arg, "import", pos)

    level, dotted = _parse_import_name(name_form, name_form, pos)

    if bind is not None:
        leaf = _IMPORT_LEAF
    elif level > 0:
        # relative imports are always leaf-style
        if dotted is None:
            raise SpecialError(
                "bare relative import requires the [MODULE as: BIND]"
                " member form",
                arg, position=pos)
        leaf = _IMPORT_LEAF
        bind = _bind_spelling(_leaf_name(dotted), _name_atoms(name_form)[1])
    else:
        if dotted is None:
            raise SpecialError(
                "import requires a module name", arg, position=pos)
        leaf = _IMPORT_ROOT
        bind = _bind_spelling(_root_name(dotted), _name_atoms(name_form)[0])

    frag = compiler.fragment(called_by.datum)
    frag.declare_var(bind)
    frag.instruction(
        OpCode.IMPORT, (dotted, level, leaf, bind))
    frag.instruction(OpCode.CONST, None)

    return frag


@builtin("compile/import-from")
@special("compile/import-from", patterns=_IMPORT_FROM_PATTERNS,
         unpack="kwargs")
def special_import_from(compiler, called_by, module, members):
    """
    Compile (import-from NAME MEMBER...) where each MEMBER is either a
    plain symbol or a [ATTR as: BIND] rename form.

    NAME is a symbol; leading dots indicate relative imports.  At least
    one member is required.  Emits a single IMPORT_FROM instruction
    carrying the module name, level, and parallel attr/bind name tuples.
    """

    pos = called_by.position
    name_sym = module.datum

    level, dotted = _parse_import_name(name_sym, name_sym, pos)

    attrs = []
    binds = []
    for elem in members.datum.unpack():
        bind, target = _parse_member(elem, elem, "import-from", pos)
        if not is_symbolish(target):
            raise SpecialError(
                f"import-from member must name an attribute, not"
                f" {target!r}",
                elem, position=pos)
        # a member names an attribute of the module, a label read by its
        # bare spelling, and the binding it lands in, which keeps the
        # template's marks so the import binds apart from the use site
        attrs.append(str(target.base))
        binds.append(bind if bind is not None else str(target))

    frag = compiler.fragment(called_by.datum)
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
@special("compile/import-all-from", patterns=_MODULE_PATTERNS,
         unpack="kwargs")
def special_import_all_from(compiler, called_by, module):
    """
    Compile (import-all-from MODULE): Python's `from MODULE import *`.

    Binds the module's public names (__all__ when defined, otherwise the
    non-underscore dir() names) into the module namespace.  Only allowed
    at module top level, matching Python.
    """

    pos = called_by.position
    name_sym = module.datum

    if compiler.scope_depth > 0:
        raise SpecialError(
            "import-all-from is only allowed at module level",
            name_sym, position=pos)

    level, dotted = _parse_import_name(name_sym, name_sym, pos)

    frag = compiler.fragment(called_by.datum)
    frag.instruction(
        OpCode.IMPORT_STAR, (dotted, level, 0, 0))
    frag.instruction(OpCode.CONST, None)

    return frag


_USE_FACET_PREFIXES = ("compile/", "layout/")


@builtin("compile/use")
@special("compile/use", patterns=_USE_PATTERNS, unpack="match")
def special_use(compiler, caps):
    """
    Compile (use MODULE) and (use MODULE STEM...), where each STEM is a
    plain symbol or a [STEM as: BIND] rename form.

    The koskari-native import: a stem names a feature, and every facet
    of it -- the runtime binding, compile/stem, layout/stem -- imports
    together, each bound under the bind stem with the same prefix, so a
    rename renames the family. A stem already carrying a facet prefix
    is literal. With no stems, lowers to import-all-from: the module's
    materialized __all__ already carries every feature's facets.

    On a Python module, whose namespace has no facet keys, use behaves
    exactly as import-from and import-all-from do. Only allowed at
    module top level.
    """

    called_by = caps["called_by"]
    pos = called_by.position
    name_sym = caps["module"].datum

    if compiler.scope_depth > 0:
        raise SpecialError(
            "use is only allowed at module level", name_sym, position=pos)

    level, dotted = _parse_import_name(name_sym, name_sym, pos)

    if "stems" not in caps:
        frag = compiler.fragment(called_by.datum)
        frag.instruction(
            OpCode.IMPORT_STAR, (dotted, level, 0, 0))
        frag.instruction(OpCode.CONST, None)
        return frag

    stems = []
    binds = []
    for elem in caps["stems"].datum.unpack():
        bind, target = _parse_member(elem, elem, "use", pos)
        if not is_symbolish(target):
            raise SpecialError(
                f"use member must name a feature, not {target!r}",
                elem, position=pos)
        stems.append(str(target.base))
        binds.append(bind if bind is not None else str(target))

    frag = compiler.fragment(called_by.datum)
    for bind in binds:
        frag.declare_var(bind)
        if not bind.startswith(_USE_FACET_PREFIXES):
            for prefix in _USE_FACET_PREFIXES:
                frag.declare_var(prefix + bind)

    stems = tuple(stems)
    binds = tuple(binds)
    if stems == binds:
        binds = stems

    frag.instruction(
        OpCode.IMPORT_USE, (dotted, level, stems, binds))
    frag.instruction(OpCode.CONST, None)

    return frag


# The end.
