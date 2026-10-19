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
koskari.match_code

The target the matcher core emits a pattern's walk into, a fragment
of the standard compiler, and the entry points that lower a pattern's
tables through it: a match arm, a whole matcher as a lambda body of
the module defining it, and a whole matcher built from Python, whose
body one shared compiler completes.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from .lib import FormalsSpec, Function, ModuleNamespace, gensym, symbol
from .match_core import Emitter


__all__ = (
    'FragmentTarget',
    'compile_match_function',
    'lower_clause',
    'lower_matcher',
    'matcher_body',
)


class FragmentTarget:
    """
    The inline target: the instructions go into a fragment of the
    standard compiler, which names the slots and lowers them to fast
    indices itself. A whole matcher lowered into it reads the subject
    as the gensym the fragment declares; an inline arm has the subject
    on the stack and names none
    """

    def __init__(self, compiler, fragment, subject=None):
        self.compiler = compiler
        self.fragment = fragment
        self.subject = subject


    def instruction(self, op, arg):
        self.fragment.instruction(op, arg)


    def label(self, text):
        return self.compiler.gen_label(text)


def lower_clause(
        compiler,
        fragment,
        tables,
        analysis,
        atoms,
        miss,
        guarded=False,
        gate_miss=None,
        after_gate=None,
        follow=None):
    """
    Emit the walk of a match arm over the subject on top of the stack.
    Each root capture is stored into its variable as soon as its value
    is tested, and a miss leaves what was stored. On a hit the walk
    falls through with the subject consumed, or for a guarded arm
    kept; on a miss it jumps to miss with the subject kept. Externs
    are read from the atoms the pattern wrote.

    :param compiler: the compiler, for labels
    :param fragment: the fragment to emit into
    :param tables: the slots, externs, consts, nodes and entry
        MatchCompiler lowered the pattern to
    :param analysis: the Analysis analyse made of the tables as an arm
    :param atoms: the extern names mapped to the atoms the pattern
        wrote them as
    :param miss: the arm's miss label
    :param guarded: whether a guard follows, which needs the subject
    :param gate_miss: where the arm's gate (arm_gate) jumps on its own
        miss, miss by default
    :param after_gate: a label to place just past the gate, or None
    :param follow: the label the caller places right after the fail
        entries, miss by default
    :return: a function the caller invokes where code after the arm's
        hit path is unreachable and just ahead of follow, emitting the
        arm's fail entries, or None when the arm has none
    :raises TypeError: when a LITERAL value or a MAP entry key is not
        a constant the pool holds
    :raises ValueError: when the pattern is one the core refuses
    """

    emitter = Emitter(
        FragmentTarget(compiler, fragment), tables, analysis, atoms,
        arm=True)
    return emitter.arm(miss, guarded, gate_miss, after_gate, follow)


def lower_matcher(target, tables, analysis, atoms):
    """
    Emit a whole matcher as a function body into a fragment target:
    the subject formal, the target's subject gensym, declared by the
    caller as the body's first local, walked as a sequence, the
    captures returned on a match, None on a miss

    :raises TypeError: when a LITERAL value or a MAP entry key is not
        a constant the pool holds
    :raises ValueError: when the pattern is one the core refuses
    """

    Emitter(target, tables, analysis, atoms).standalone()


def matcher_body(compiler, tables, analysis, atoms, name):
    """
    A whole matcher lowered into a function body fragment of compiler:
    every extern the pattern names requested under the atom the pattern
    wrote, or its plain symbol, and the subject a gensym declared as the
    body's one local, ready to complete as any lambda body

    :param compiler: the compiler the body is a fragment of
    :param tables: the slots, externs, consts, nodes and entry
        MatchCompiler lowered the pattern to
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


def compile_match_function(tables, analysis, base_ns):
    """
    The pattern's tables as a Function over base_ns: one positional
    formal, the subject; the externs closure names resolved in base_ns;
    the captures returned on a match and None on a miss

    :param tables: the slots, externs, consts, nodes and entry
        MatchCompiler lowered the pattern to
    :param analysis: the Analysis analyse made of the tables
    :param base_ns: the namespace the externs resolve in
    """

    global standalone_compiler

    if standalone_compiler is None:
        from .compile.compiler import Compiler
        standalone_compiler = Compiler.from_pragma(
            "matcher", ModuleNamespace("matcher", {}))
    compiler = standalone_compiler
    body, subject = matcher_body(compiler, tables, analysis, {}, "matcher")
    code = compiler.complete_function(body, ())
    return Function(code, base_ns, FormalsSpec((str(subject),)))


# The end.
