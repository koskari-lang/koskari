# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License along with this library.  If not, see
# <http://www.gnu.org/licenses/>.

"""
tests.compile.test_convert_fast

Unit tests for convert_fast VAR to FAST lowering in the compiler.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

from koskari.compile import CompilerMode
from koskari.compile.processor import ByteCodeProcessor
from koskari.lib import OpCode, NamespaceSpec

import pytest

pytestmark = pytest.mark.phase_compile


def convert_fast(nspec, instr, mode=None):
    proc = ByteCodeProcessor(instr, mode=mode)
    proc.convert_fast(nspec)
    return proc.instructions


class TestConvertFastVarToFast(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers VAR opcodes to FAST when the variable is local or
    closure
    """

    async def test_call_var_attr_names_with_local_receiver(self):
        """
        CALL_VAR_ATTR_NAMES with local receiver converts to CALL_FAST_ATTR_NAMES
        """
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.CALL_VAR_ATTR_NAMES, ("obj", "upper", (), 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST_ATTR_NAMES)
        self.assertEqual(arg, (0, "upper", (), 0))


    async def test_call_var_attr_names_with_non_local_receiver(self):
        """
        CALL_VAR_ATTR_NAMES with receiver not in findex remains unchanged
        """
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.CALL_VAR_ATTR_NAMES, ("other", "upper", (), 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_VAR_ATTR_NAMES)
        self.assertEqual(arg, ("other", "upper", (), 0))


    async def test_simple_var_opcode_still_works(self):
        """
        Scalar VAR opcode converts to FAST when name is in findex
        """
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.VAR, "obj")]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.FAST)
        self.assertEqual(arg, 0)


    async def test_call_var_attr_names_with_closure_receiver(self):
        """
        CALL_VAR_ATTR_NAMES with closure receiver converts to CALL_FAST_ATTR_NAMES
        """
        nspec = NamespaceSpec(None, (), ("obj",))
        instr = [(OpCode.CALL_VAR_ATTR_NAMES, ("obj", "upper", (), 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST_ATTR_NAMES)
        self.assertEqual(arg, (0, "upper", (), 0))


    async def test_no_conversion_when_nstack_empty(self):
        """
        CALL_VAR_ATTR_NAMES is not converted when nstack is empty
        """
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.CALL_VAR_ATTR_NAMES, ("obj", "upper", (), 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.EXPRESSION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_VAR_ATTR_NAMES)
        self.assertEqual(arg, ("obj", "upper", (), 0))


    async def test_var_store_const_lowers_to_fast_store_const(self):
        """
        VAR_STORE_CONST with local name converts to FAST_STORE_CONST
        """
        nspec = NamespaceSpec(None, ("x",), ())
        instr = [(OpCode.VAR_STORE_CONST, ("x", 99, None, None))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.FAST_STORE_CONST)
        self.assertEqual(arg, (0, 99, None, None))


    async def test_var_store_const_non_local_unchanged(self):
        """
        VAR_STORE_CONST with name not in findex remains unchanged
        """
        nspec = NamespaceSpec(None, ("x",), ())
        instr = [(OpCode.VAR_STORE_CONST, ("other", 99, None, None))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.VAR_STORE_CONST)
        self.assertEqual(arg, ("other", 99, None, None))


class TestConvertFastCallVar(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers CALL_VAR* to CALL_FAST* when name is local
    """

    async def test_call_var_with_local_converts_to_call_fast(self):
        """
        CALL_VAR with local name converts to CALL_FAST
        """
        nspec = NamespaceSpec(None, ("f",), ())
        instr = [(OpCode.CALL_VAR, ("f", 2, 0, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST)
        self.assertEqual(arg, (0, 2, 0, 0))


    async def test_call_var_names_with_local_converts_to_call_fast_names(self):
        """
        CALL_VAR_NAMES with local name converts to CALL_FAST_NAMES
        """
        nspec = NamespaceSpec(None, ("f",), ())
        instr = [(OpCode.CALL_VAR_NAMES, ("f", ("name",), 1, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST_NAMES)
        self.assertEqual(arg, (0, ("name",), 1, 0))


    async def test_call_var_star_with_local_converts_to_call_fast_star(self):
        """
        CALL_VAR_STAR with local name converts to CALL_FAST_STAR
        """
        nspec = NamespaceSpec(None, ("f",), ())
        instr = [(OpCode.CALL_VAR_STAR, ("f", 3, 0, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST_STAR)
        self.assertEqual(arg, (0, 3, 0, 0))


    async def test_call_var_with_non_local_remains_unchanged(self):
        """
        CALL_VAR with non-local name remains CALL_VAR
        """
        nspec = NamespaceSpec(None, ("a", "b"), ())
        instr = [(OpCode.CALL_VAR, ("other", 1, 0, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_VAR)
        self.assertEqual(arg, ("other", 1, 0, 0))


    async def test_call_var_attr_with_local_converts_to_call_fast_attr(self):
        """
        CALL_VAR_ATTR with local receiver converts to CALL_FAST_ATTR
        """
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.CALL_VAR_ATTR, ("obj", "upper", 0, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST_ATTR)
        self.assertEqual(arg, (0, "upper", 0, 0))


    async def test_call_var_attr_with_non_local_remains_unchanged(self):
        """
        CALL_VAR_ATTR with receiver not in findex remains unchanged
        """
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.CALL_VAR_ATTR, ("other", "upper", 0, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_VAR_ATTR)
        self.assertEqual(arg, ("other", "upper", 0, 0))


    async def test_call_var_attr_star_with_local_converts_to_call_fast_attr_star(self):
        """
        CALL_VAR_ATTR_STAR with local receiver converts to CALL_FAST_ATTR_STAR
        """
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.CALL_VAR_ATTR_STAR, ("obj", "m", 3, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST_ATTR_STAR)
        self.assertEqual(arg, (0, "m", 3, 0))


    async def test_call_var_attr_with_closure_receiver(self):
        """
        CALL_VAR_ATTR with closure receiver converts to CALL_FAST_ATTR
        """
        nspec = NamespaceSpec(None, (), ("obj",))
        instr = [(OpCode.CALL_VAR_ATTR, ("obj", "add_one", 1, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST_ATTR)
        self.assertEqual(arg, (0, "add_one", 1, 0))


    async def test_call_var_attr_star_with_closure_receiver(self):
        """
        CALL_VAR_ATTR_STAR with closure receiver converts to CALL_FAST_ATTR_STAR
        """
        nspec = NamespaceSpec(None, (), ("obj",))
        instr = [(OpCode.CALL_VAR_ATTR_STAR, ("obj", "m", 2, 0))]
        result = convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.CALL_FAST_ATTR_STAR)
        self.assertEqual(arg, (0, "m", 2, 0))


    async def test_call_var_attr_no_conversion_in_expression_mode(self):
        """
        CALL_VAR_ATTR and CALL_VAR_ATTR_STAR are not converted when nstack is empty
        """
        nspec = NamespaceSpec(None, ("obj",), ())
        for op_in, arg_in in (
                (OpCode.CALL_VAR_ATTR, ("obj", "upper", 0, 0)),
                (OpCode.CALL_VAR_ATTR_STAR, ("obj", "m", 3, 0)),
        ):
            instr = [(op_in, arg_in)]
            result = convert_fast(
                nspec, instr, mode=CompilerMode.EXPRESSION)
            self.assertEqual(len(result), 1)
            op, arg = result[0]
            self.assertEqual(op, op_in)
            self.assertEqual(arg, arg_in)


# The end.
