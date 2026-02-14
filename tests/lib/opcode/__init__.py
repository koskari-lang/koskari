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

Provides build_code, build_expr, run_to_completion, _op, and CevalTest
for tests under tests.lib.opcode.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

from koskari.lib import (
    Code,
    NamespaceSpec,
    OpCode,
)
from tests import make_callable


def _op(name):
    """
    Map string opcode name to OpCode enum (e.g. CONST -> KOS_OP_CONST)
    """

    return getattr(OpCode, "KOS_OP_" + name)


def build_code(code, local_syms=None, name="expr"):
    """
    Build Code from a list of [opname, arg] with string opcode names

    :param code: Sequence of [op, arg] (e.g. [["CONST", 42], ["RETURN", 1]])
    :param local_syms: Optional sequence of local names for the namespace spec
    :param name: Code name for debugging
    :return: Code instance
    """

    if local_syms is None:
        local_syms = ()
    elif not isinstance(local_syms, (tuple, list)):
        local_syms = (local_syms,)
    nspec = NamespaceSpec(None, tuple(local_syms), ())
    instr = tuple([_op(op), arg] for op, arg in code)
    return Code(nspec, instr, name)


def build_expr(code, local_syms=None, values=None, name="expr"):
    """
    Build Code and namespace; optionally bind names (for VAR) via values

    :param code: Sequence of [op, arg] as in build_code
    :param local_syms: Optional sequence of local names
    :param values: Optional dict of name -> value to store in the namespace
    :param name: Code name
    :return: (code, ns) for use with make_callable(code, ns)()
    """

    if values:
        syms = set(local_syms or ()) | set(values.keys())
        local_syms = tuple(syms)
    c = build_code(code, local_syms=local_syms, name=name)
    nspec = NamespaceSpec(None, tuple(local_syms or ()), ())
    ns = nspec.get_namespace(None)
    if values:
        for k, v in values.items():
            ns.store(k, v)
    return (c, ns)


def run_to_completion(coro):
    """
    Drive the coroutine until StopIteration; return the yielded value

    :param coro: Coroutine from make_callable(code, ns)() or similar
    :return: The value passed to StopIteration
    """

    try:
        while True:
            coro.send(None)
    except StopIteration as e:
        return e.value


class CevalTest(unittest.TestCase):
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
        Assert stub opcode raises RuntimeError (op_unimplemented_stub)
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
