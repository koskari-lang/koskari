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

The targets the matcher core emits a pattern's walk into, and the
entry points that lower a pattern's tables through it. The inline
target emits into a fragment of the standard compiler, for a match
arm or for a whole matcher as a lambda body. The standalone target
builds a Function of its own from the instructions, with no compiler
in reach, whose result is the captures.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from .lib import (
    Code, FormalsSpec, Function, NamespaceSpec, OpCode, OpStyle, gensym,
    symbol)
from .match_core import Emitter


__all__ = (
    'FragmentTarget',
    'FunctionTarget',
    'compile_match_function',
    'lower_clause',
    'lower_matcher',
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


# the simple-style VAR instructions the standalone target lowers to
# their FAST twins, beside the MATCH_* twins named by their spelling
SIMPLE_TWINS = {
    OpCode.VAR: OpCode.FAST,
    OpCode.CALL_VAR: OpCode.CALL_FAST,
}


class FunctionTarget:
    """
    The standalone target: an instruction list and labels of its own,
    built into a Function over a namespace the externs resolve in. The
    subject, its one formal and its one local, is a gensym minted here
    """

    def __init__(self, name):
        self.name = name
        self.subject = gensym()
        self.instructions = []
        self.counter = 0


    def instruction(self, op, arg):
        self.instructions.append((op, arg))


    def label(self, text):
        self.counter += 1
        return f"{text}_{self.counter:x}"


    def build(self, externs, base_ns):
        """
        The Function: the subject its one formal and its one local, the
        externs its closure names resolved in base_ns

        :param externs: the extern atoms, in extern-table order
        :param base_ns: the namespace the externs resolve in
        """

        subject = str(self.subject)
        closure_syms = tuple(str(e) for e in externs)
        findex = [subject, *closure_syms]

        lowered = []
        for op, arg in self.instructions:
            name = op.name
            twin = SIMPLE_TWINS.get(op)
            if twin is None and name.startswith('MATCH_') \
                    and 'VAR' in name.split('_'):
                twin = OpCode[name.replace('VAR', 'FAST')]
            if twin is not None:
                if op.extra:
                    arg = tuple(
                        findex.index(str(v)) if fk == OpStyle.VAR else v
                        for v, fk in zip(arg, op.extra))
                else:
                    arg = findex.index(str(arg))
                op = twin
            lowered.append((op, arg))

        nspec = NamespaceSpec(self.name, (subject,), closure_syms)
        code = Code(nspec, lowered, self.name)
        return Function(code, base_ns, FormalsSpec((subject,)))


def lower_clause(compiler, fragment, tables, atoms, miss):
    """
    Emit the walk of a match arm over the subject on top of the stack,
    which stays there: on a hit each filled root slot is written to
    its variable and the walk falls through, on a miss it jumps to miss
    writing nothing. Externs are read from the atoms the pattern wrote.

    :param compiler: the compiler, for labels
    :param fragment: the fragment to emit into
    :param tables: the slots, externs, consts, nodes and entry
        MatchCompiler lowered the pattern to
    :param atoms: the extern names mapped to the atoms the pattern
        wrote them as
    :param miss: the arm's miss label
    :return: a function the caller invokes where code after the arm's
        hit path is unreachable, emitting the arm's fail blocks
    :raises TypeError: when a LITERAL value or a MAP entry key is not
        a constant the pool holds
    :raises ValueError: when the pattern is one the core refuses
    """

    emitter = Emitter(
        FragmentTarget(compiler, fragment), tables, atoms, arm=True)
    return emitter.arm(miss)


def lower_matcher(target, tables, atoms):
    """
    Emit a whole matcher as a function body into either target: the
    subject formal, the target's subject gensym, declared by the caller
    as the body's first local, walked as a sequence, the captures
    returned on a match, None on a miss. Into a fragment of the
    standard compiler the body becomes a lambda's; into a
    FunctionTarget it becomes a Function with no compiler in reach

    :raises TypeError: when a LITERAL value or a MAP entry key is not
        a constant the pool holds
    :raises ValueError: when the pattern is one the core refuses
    """

    Emitter(target, tables, atoms).standalone()


def compile_match_function(tables, base_ns):
    """
    The pattern's tables as a Function over base_ns: one positional
    formal, the subject; the externs closure names resolved in base_ns;
    the captures returned on a match and None on a miss

    :param tables: the slots, externs, consts, nodes and entry
        MatchCompiler lowered the pattern to
    :param base_ns: the namespace the externs resolve in
    """

    externs = [symbol(n) for n in tables[1]]
    target = FunctionTarget("matcher")
    lower_matcher(target, tables, {})
    return target.build(externs, base_ns)


# The end.
