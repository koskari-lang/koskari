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
Shared helpers and base class for opcode tests.

Provides build_code, build_expr, run_to_completion, _op, CevalTest, and
run_asyncio_in_thread for tests under tests.lib.opcode.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import (
    Code,
    NamespaceSpec,
    OpCode,
    Operand,
)
from tests import make_callable


def opcount(operand, count):
    """
    Build the merged OPCOUNT arg for an OBJECT_OPERAND instruction: the
    operator selector in the high byte and the operand count in the low byte.

    :param operand: Operand member (or its int value)
    :param count: operand count (0..255)
    :return: merged uint16 int
    """

    return (int(operand) << 8) | (int(count) & 0xff)


# The per-operator arithmetic opcodes were consolidated into two parameterized
# opcodes: the reducing/binary family into OBJECT_OPERAND and the stack in-place
# family into OBJECT_INPLACE (dispatched to the KosOperator_* functions).
# build_code accepts the
# readable per-operator names as sugar and lowers them -- the same lowering the
# compiler performs -- so bytecode-level tests keep expressing the operation
# directly while exercising the real opcode. Each maps its per-operator name to
# the Operand op-type; the arg supplies the operand count.
_OPERAND_BY_NAME = {
    "ADD": Operand.KOS_OPERAND_ADD,
    "SUBTRACT": Operand.KOS_OPERAND_SUBTRACT,
    "MULTIPLY": Operand.KOS_OPERAND_MULTIPLY,
    "MODULO": Operand.KOS_OPERAND_MODULO,
    "POWER": Operand.KOS_OPERAND_POWER,
    "TRUE_DIVIDE": Operand.KOS_OPERAND_TRUE_DIVIDE,
    "FLOOR_DIVIDE": Operand.KOS_OPERAND_FLOOR_DIVIDE,
    "MATRIX_MULTIPLY": Operand.KOS_OPERAND_MATRIX_MULTIPLY,
    "BIT_AND": Operand.KOS_OPERAND_BIT_AND,
    "BIT_OR": Operand.KOS_OPERAND_BIT_OR,
    "BIT_XOR": Operand.KOS_OPERAND_BIT_XOR,
    "BIT_LEFT": Operand.KOS_OPERAND_BIT_LEFT,
    "BIT_RIGHT": Operand.KOS_OPERAND_BIT_RIGHT,
}

_OPERAND_LOWER = {
    "OBJECT_" + suffix: (operand, "OBJECT_OPERAND")
    for suffix, operand in _OPERAND_BY_NAME.items()
}
_OPERAND_LOWER.update({
    "OBJECT_INPLACE_" + suffix: (operand, "OBJECT_INPLACE")
    for suffix, operand in _OPERAND_BY_NAME.items()
})


def _op(name):
    """
    Map string opcode name to OpCode enum (e.g. CONST -> KOS_OP_CONST)
    """

    return getattr(OpCode, "KOS_OP_" + name)


def _lower_instr(op, arg):
    """
    Lower one [opname, arg] pair to (OpCode, arg), translating a consolidated
    per-operator arithmetic name (OBJECT_<op> / OBJECT_INPLACE_<op>) into its
    parameterized opcode (OBJECT_OPERAND / OBJECT_INPLACE) with an OPCOUNT arg.
    """

    lowered = _OPERAND_LOWER.get(op)
    if lowered is not None:
        operand, opname = lowered
        return [_op(opname), opcount(operand, arg)]
    return [_op(op), arg]


def build_code(code, local_syms=None, closure_syms=None, name="expr"):
    """
    Build Code from a list of [opname, arg] with string opcode names

    Consolidated per-operator arithmetic names (OBJECT_ADD, OBJECT_MODULO,
    ...) are accepted as sugar and lowered to OBJECT_OPERAND (see
    _OPERAND_LOWER); the arg supplies the operand count.

    :param code: Sequence of [op, arg] (e.g. [["CONST", 42], ["RETURN", 1]])
    :param local_syms: Optional sequence of local names for the namespace spec
    :param closure_syms: Optional sequence of closure names (slot indices after
        local_syms)
    :param name: Code name for debugging
    :return: Code instance
    """

    if local_syms is None:
        local_syms = ()
    elif not isinstance(local_syms, (tuple, list)):
        local_syms = (local_syms,)
    if closure_syms is None:
        closure_syms = ()
    elif not isinstance(closure_syms, (tuple, list)):
        closure_syms = (closure_syms,)
    nspec = NamespaceSpec(None, tuple(local_syms), tuple(closure_syms))
    instr = tuple(_lower_instr(op, arg) for op, arg in code)
    return Code(nspec, instr, name)


def build_expr(code, local_syms=None, closure_syms=None, values=None, name="expr"):
    """
    Build Code and namespace; optionally bind names (for VAR) via values

    :param code: Sequence of [op, arg] as in build_code
    :param local_syms: Optional sequence of local names
    :param closure_syms: Optional sequence of closure names (passed to build_code)
    :param values: Optional dict of name -> value to store in the namespace
    :param name: Code name
    :return: (code, ns) for use with make_callable(code, ns)()
    """

    if values:
        syms = set(local_syms or ()) | set(closure_syms or ()) | set(values.keys())
        local_syms = tuple(syms)
    c = build_code(code, local_syms=local_syms, closure_syms=closure_syms, name=name)
    nspec = NamespaceSpec(None, local_syms, closure_syms)
    ns = nspec.get_namespace(None)
    if values:
        for k, v in values.items():
            ns.store(k, v)
    return (c, ns)


class CevalTest(unittest.IsolatedAsyncioTestCase):
    """
    Base class with assertion helpers for opcode tests
    """

    def assertRuntimeError(self, *args, **kwds):
        """
        Assert that calling the given expression raises RuntimeError
        """

        self.assertRaises(RuntimeError, *args, **kwds)


    def assertTypeError(self, *args, **kwds):
        """
        Assert that calling the given expression raises TypeError
        """

        self.assertRaises(TypeError, *args, **kwds)


    def assertStubRaises(self, *args, **kwds):
        """
        Assert stub opcode raises RuntimeError
        """

        self.assertRaises(RuntimeError, *args, **kwds)


    def _stop_async_iteration_final(self, ctx):
        """
        Extract final value from StopAsyncIteration exception context.
        Works across Python versions (e.g. .value on 3.13+ vs args[0] on 3.12).
        """

        exc = ctx.exception
        return getattr(exc, "value", None) or (
            exc.args[0] if exc.args else None
        )


# The end.
