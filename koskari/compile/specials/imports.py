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

Special form handlers for compile/import and compile/import-from.

These specials translate Koskari import forms into sequences of the IMPORT,
IMPORT_REL, and IMPORT_FROM opcodes, with compensating POP instructions.

IMPORT_FROM PEEK-NOT-POP CONTRACT
==================================
IMPORT_FROM is the only opcode in the koskari set that does not consume TOS.
The module pushed by IMPORT or IMPORT_REL remains on the stack after every
IMPORT_FROM instruction.  A balancing POP 1 MUST follow every sequence of
IMPORT_FROM instructions.

This invariant is structurally enforced: IMPORT_FROM instructions are only
emitted by helper_import_from_sequence(), which is the sole call site in
the entire codebase.  helper_import_from_sequence() always emits POP 1 as
its final instruction.  Do not emit IMPORT_FROM anywhere else.

Opcode sequences produced
==========================
(import foo)
    IMPORT "foo" / VAR_STORE "foo" / CONST None

(import foo.bar)
    IMPORT "foo.bar" / VAR_STORE "bar" / CONST None       (leaf binding)

(import foo.bar as: baz)
    IMPORT "foo.bar" / VAR_STORE "baz" / CONST None

(import .foo)
    IMPORT_REL (1, "foo") / VAR_STORE "foo" / CONST None

(import ..foo.bar)
    IMPORT_REL (2, "foo.bar") / VAR_STORE "bar" / CONST None

(import-from foo bar baz)
    IMPORT "foo"
    IMPORT_FROM "bar" / VAR_STORE "bar"
    IMPORT_FROM "baz" / VAR_STORE "baz"
    POP 1
    CONST None

(import-from foo [bar as: b])
    IMPORT "foo"
    IMPORT_FROM "bar" / VAR_STORE "b"
    POP 1
    CONST None

(import-from .foo bar)
    IMPORT_REL (1, "foo")
    IMPORT_FROM "bar" / VAR_STORE "bar"
    POP 1
    CONST None

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...exceptions import SpecialError
from ...lib import OpCode, nil
from ...matcher import CALLEDBY, MATCH, REST
from ...bootstrap import builtin
from ...syntax import special
from ...types import is_keyword, is_pair, is_symbol, unpack
from ..helpers import helper_using, is_symbolish


__all__ = (
    'helper_import_from_sequence',
    'special_import',
    'special_import_from',
)


_DEFAULT_PATTERNS = (MATCH(CALLEDBY(), REST("rest")),)


def _attr_form_to_dotted(form, source):
    """
    Convert (compile/attr obj name) or nested equivalent to dotted string.

    :param form: (compile/attr obj name) with obj symbol or nested attr
    :param source: source form for error context
    :return: str like "os.path" or "foo.bar.baz"
    :raises SpecialError: when form is invalid
    """

    if is_symbol(form) or isinstance(form, str):
        return str(form)

    if not is_pair(form):
        raise SpecialError(
            "import argument must be symbol or (compile/attr ...)",
            source)

    head, tail = form
    if str(head) != 'compile/attr':
        raise SpecialError(
            "import argument must be symbol or (compile/attr ...)",
            source)

    parts = list(unpack(tail))
    if len(parts) < 2:
        raise SpecialError(
            "compile/attr in import requires two arguments",
            source)

    result = []
    for p in parts:
        if is_symbol(p) or isinstance(p, str):
            result.append(str(p))
        elif is_pair(p) and str(p[0]) == 'compile/attr':
            result.append(_attr_form_to_dotted(p, source))
        else:
            raise SpecialError(
                "import compile/attr arguments must be symbols",
                source)

    return '.'.join(result)


def _parse_import_name(name_form, source):
    """
    Split a module name (symbol or (compile/attr ...)) into (level:int,
    dotted_name:str).

    Counts leading dots for relative imports and strips them from the
    returned name.  A bare dot sequence (level > 0, no name) returns
    (level, None) representing an import of the enclosing package itself.

    :param name_form: symbol like 'foo', 'foo.bar', '.foo' or (compile/attr
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

    if is_pair(name_form) and str(name_form[0]) == 'compile/attr':
        dotted = _attr_form_to_dotted(name_form, source)
        return (0, dotted)

    raise SpecialError(
        "import argument must be a symbol or (compile/attr ...)",
        source)


def _leaf_name(dotted):
    """
    Return the last component of a dotted module name, or the name itself.

    :param dotted: str such as "foo.bar.baz" or "foo"
    :return: str "baz" or "foo"
    """

    return dotted.rsplit('.', 1)[-1]


def helper_import_from_sequence(frag, import_frag, members):
    """
    Emit a full import-from sequence with compensating POP 1.

    This is THE ONLY function that emits IMPORT_FROM opcodes.  It always
    terminates the sequence with POP 1 to remove the module pushed by
    import_frag, preserving stack balance.

    Each entry in members is a (bind_name:str, attr_name:str) pair.

    Stack evolution after this helper returns:
        before: [...]
        after:  [..., None]       (CONST None is the statement value)

    :param frag: ByteCodeFragment to emit into
    :param import_frag: fragment that pushes the module (IMPORT or IMPORT_REL)
    :param members: list of (bind_name, attr_name) pairs
    """

    # Push the module
    frag.merge(import_frag)

    # For each member: PEEK TOS, get attr, bind
    for bind_name, attr_name in members:
        frag.declare_var(bind_name)
        frag.instruction(OpCode.KOS_OP_IMPORT_FROM, attr_name)
        frag.instruction(OpCode.KOS_OP_VAR_STORE, bind_name)

    # Compensate for the PEEK-NOT-POP behaviour: remove the module
    frag.instruction(OpCode.KOS_OP_POP, 1)

    # Statement value
    frag.instruction(OpCode.KOS_OP_CONST, None)


def _parse_member(elem, source):
    """
    Parse one element from an (import-from ...) member list.

    Accepted forms:
        SYM               -> bind as SYM, read attr SYM
        [ATTR as: BIND]   -> bind as BIND, read attr ATTR

    :param elem: one element from the member list
    :param source: source form for error context
    :return: (bind_name:str, attr_name:str)
    :raises SpecialError: on invalid member form
    """

    if is_symbolish(elem):
        name = str(elem)
        return (name, name)

    if is_pair(elem):
        try:
            name, value = helper_using(elem)
        except SpecialError:
            raise SpecialError(
                "import-from member must be a symbol or [ATTR as: BIND], not %r"
                % (elem,),
                source) from None

        if name is None:
            if not is_symbolish(value):
                raise SpecialError(
                    "import-from member must be a symbol or [ATTR as: BIND],"
                    " not %r" % (elem,),
                    source)
            s = str(value)
            return (s, s)

        if not is_symbolish(name) or not is_symbolish(value):
            raise SpecialError(
                "import-from member [ATTR as: BIND] requires symbols, not %r"
                % (elem,),
                source)
        return (str(name), str(value))

    raise SpecialError(
        "import-from member must be a symbol or [ATTR as: BIND], not %r"
        % (elem,),
        source)


@builtin("compile/import")
@special("compile/import", patterns=_DEFAULT_PATTERNS)
def special_import(compiler, source, tc=False):
    """
    Compile (import NAME) and (import NAME as: ALIAS).

    NAME is a symbol; leading dots indicate relative imports.  For dotted
    names the leaf component is bound unless as: ALIAS is given.
    """

    called_by, rest = source
    if rest is nil:
        raise SpecialError("import requires a module name", source)

    name_sym, rest = rest

    # Optional as: ALIAS suffix
    alias = None
    if rest is not nil:
        maybe_as, rest2 = rest
        if is_keyword(maybe_as) and str(maybe_as) == 'as':
            if rest2 is nil:
                raise SpecialError("import as: requires an alias symbol", source)

            alias_sym, rest = rest2
            if not is_symbolish(alias_sym):
                raise SpecialError("import alias must be a symbol", source)

            alias = str(alias_sym)
        # else: rest still has content; will be caught by too-many-args check

    if rest is not nil:
        raise SpecialError("too many arguments to import", source)

    level, dotted = _parse_import_name(name_sym, source)

    frag = compiler.fragment(called_by)

    if level == 0:
        # Absolute import
        frag.instruction(OpCode.KOS_OP_IMPORT, dotted)
    else:
        frag.instruction(OpCode.KOS_OP_IMPORT_REL, (level, dotted))

    bind_name = alias if alias else (
        _leaf_name(dotted) if dotted else None
    )
    if bind_name is None:
        raise SpecialError(
            "bare relative import (without name) requires as: ALIAS",
            source)

    frag.declare_var(bind_name)
    frag.instruction(OpCode.KOS_OP_VAR_STORE, bind_name)
    frag.instruction(OpCode.KOS_OP_CONST, None)

    return frag


@builtin("compile/import-from")
@special("compile/import-from", patterns=_DEFAULT_PATTERNS)
def special_import_from(compiler, source, tc=False):
    """
    Compile (import-from NAME MEMBER...) where each MEMBER is either a
    plain symbol or a [ATTR as: BIND] rename form.

    NAME is a symbol; leading dots indicate relative imports.  At least
    one member is required.
    """

    called_by, rest = source
    if rest is nil:
        raise SpecialError("import-from requires a module name", source)

    name_sym, rest = rest
    if rest is nil:
        raise SpecialError("import-from requires at least one member", source)

    level, dotted = _parse_import_name(name_sym, source)

    members = []
    current = rest
    while current is not nil:
        elem, current = current
        members.append(_parse_member(elem, source))

    import_frag = compiler.fragment(called_by)
    if level == 0:
        import_frag.instruction(OpCode.KOS_OP_IMPORT, dotted)
    else:
        import_frag.instruction(OpCode.KOS_OP_IMPORT_REL, (level, dotted))

    frag = compiler.fragment(called_by)
    helper_import_from_sequence(frag, import_frag, members)

    return frag


# The end.
