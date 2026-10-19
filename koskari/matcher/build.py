# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
koskari.matcher.build

The completion of a matcher body into its function: the body lowered
into a fragment of a compiler, the defining module's for a syntax
special and one shared compiler for a matcher built from Python, and
the first-match function that tries matchers in order

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from koskari.lib import (
    Code, Compare, FormalsSpec, Function, ModuleNamespace, NamespaceSpec,
    OpCode, demand, gensym, symbol)

from .emitter import FragmentTarget, lower_matcher


__all__ = (
    'FIRST_MATCH',
    'build_matcher',
    'first_match',
    'matcher_body',
)


def matcher_body(compiler, tables, analysis, atoms, name):
    """
    A whole matcher lowered into a function body fragment of compiler:
    every extern the pattern names requested under the atom the pattern
    wrote, or its plain symbol, and the subject a gensym declared as the
    body's one local, ready to complete as any lambda body

    :param compiler: the compiler the body is a fragment of
    :param tables: the slots, externs, consts, nodes and entry
        TableBuilder lowered the pattern to
    :param analysis: the Analysis analyse made of the tables
    :param atoms: the extern names mapped to the atoms the pattern
        wrote them as
    :param name: the body's name
    :return: the body fragment and its subject gensym
    :raises TypeError: when a LITERAL value or a MAP entry key is not
        a constant the pool holds
    :raises ValueError: when the pattern is one the core refuses
    """

    body = compiler.fragment(name)
    for extern in tables[1]:
        body.request_var(atoms.get(extern) or symbol(extern))
    # minted after the extern atoms are interned and held by the
    # requests, so its spelling is none of theirs
    subject = gensym()
    body.declare_var(subject)
    with compiler.resolution_scope(body, function=True):
        lower_matcher(
            FragmentTarget(compiler, body, subject), tables, analysis, atoms)
    return body, subject


# the compiler that completes the bodies of matchers built from
# Python, made on the first such build: a body names no module value
# and the Function is bound to its own build's namespace
standalone_compiler = None


def build_matcher(tables, analysis, base_ns):
    """
    The pattern's tables as a Function over base_ns: one positional
    formal, the subject; the externs closure names resolved in base_ns;
    the captures returned on a match and None on a miss

    :param tables: the slots, externs, consts, nodes and entry
        TableBuilder lowered the pattern to
    :param analysis: the Analysis analyse made of the tables
    :param base_ns: the namespace the externs resolve in
    """

    global standalone_compiler

    if standalone_compiler is None:
        from koskari.compile.compiler import Compiler
        standalone_compiler = Compiler.from_pragma(
            "matcher", ModuleNamespace("matcher", {}))
    compiler = standalone_compiler
    body, subject = matcher_body(compiler, tables, analysis, {}, "matcher")
    code = compiler.complete_function(body, ())
    return Function(code, base_ns, FormalsSpec((str(subject),)))


# the first arm whose matcher matches a subject: called with the
# subject and a tuple of the arms' matcher callables, it calls each in
# order and returns the index and the captures of the first result
# that is not None, or -1 and None when none is. The fast slots are
# subject, callables, index, fn and captures, then enumerate
FIRST_MATCH = Function(
    Code(
        NamespaceSpec(
            "first-match",
            ("subject", "callables", "index", "fn", "captures"),
            ("enumerate",)),
        [
            (OpCode.FAST, 1),
            (OpCode.CALL_FAST, (5, 1, 0, 0)),
            (OpCode.PUSH_FOREACH, None),
            (OpCode.ITER_NEXT, "first-match.miss"),
            (OpCode.UNPACK_FAST, (2, 3, 0, 0)),
            (OpCode.FAST, 0),
            (OpCode.CALL_FAST, (3, 1, 0, 0)),
            (OpCode.FAST_STORE, 4),
            (OpCode.FAST, 4),
            (OpCode.CONST, None),
            (OpCode.CHECK_COMPARE,
             (Compare.IS_NOT, 2, "first-match.next", 0)),
            (OpCode.FAST2, (2, 4, 0, 0)),
            (OpCode.RETURN, 2),
            (OpCode.LABEL, "first-match.next"),
            (OpCode.CONTINUE, 0),
            (OpCode.LABEL, "first-match.miss"),
            (OpCode.RETURN_CONST, (-1, None)),
            (OpCode.POP_FOREACH, None),
            (OpCode.RETURN, 0),
        ],
        "first-match"),
    ModuleNamespace("first-match", {"enumerate": enumerate}),
    FormalsSpec(("subject", "callables")))

# FIRST_MATCH for the Python callers, reusing the context it runs in
first_match = demand(FIRST_MATCH)


# The end.
