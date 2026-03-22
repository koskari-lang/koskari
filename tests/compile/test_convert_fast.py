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

from koskari.compile import Compiler, CompilerMode
from koskari.lib import OpCode
from koskari.types import NamespaceSpec

import pytest

pytestmark = pytest.mark.phase_compile


class TestConvertFastVarToFast(unittest.IsolatedAsyncioTestCase):
    """
    convert_fast lowers VAR opcodes to FAST when the variable is local or
    closure
    """

    async def test_call_var_attr_names_with_local_receiver(self):
        """
        CALL_VAR_ATTR_NAMES with local receiver converts to CALL_FAST_ATTR_NAMES
        """
        compiler = Compiler("<test>", None)
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.KOS_OP_CALL_VAR_ATTR_NAMES, ("obj", "upper", (), 0))]
        result = compiler.convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.KOS_OP_CALL_FAST_ATTR_NAMES)
        self.assertEqual(arg, (0, "upper", (), 0))


    async def test_call_var_attr_names_with_non_local_receiver(self):
        """
        CALL_VAR_ATTR_NAMES with receiver not in findex remains unchanged
        """
        compiler = Compiler("<test>", None)
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.KOS_OP_CALL_VAR_ATTR_NAMES, ("other", "upper", (), 0))]
        result = compiler.convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.KOS_OP_CALL_VAR_ATTR_NAMES)
        self.assertEqual(arg, ("other", "upper", (), 0))


    async def test_simple_var_opcode_still_works(self):
        """
        Scalar VAR opcode converts to FAST when name is in findex
        """
        compiler = Compiler("<test>", None)
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.KOS_OP_VAR, "obj")]
        result = compiler.convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.KOS_OP_FAST)
        self.assertEqual(arg, 0)


    async def test_call_var_attr_names_with_closure_receiver(self):
        """
        CALL_VAR_ATTR_NAMES with closure receiver converts to CALL_FAST_ATTR_NAMES
        """
        compiler = Compiler("<test>", None)
        nspec = NamespaceSpec(None, (), ("obj",))
        instr = [(OpCode.KOS_OP_CALL_VAR_ATTR_NAMES, ("obj", "upper", (), 0))]
        result = compiler.convert_fast(nspec, instr, mode=CompilerMode.FUNCTION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.KOS_OP_CALL_FAST_ATTR_NAMES)
        self.assertEqual(arg, (0, "upper", (), 0))


    async def test_no_conversion_when_nstack_empty(self):
        """
        CALL_VAR_ATTR_NAMES is not converted when nstack is empty
        """
        compiler = Compiler("<test>", None)
        nspec = NamespaceSpec(None, ("obj",), ())
        instr = [(OpCode.KOS_OP_CALL_VAR_ATTR_NAMES, ("obj", "upper", (), 0))]
        result = compiler.convert_fast(nspec, instr, mode=CompilerMode.EXPRESSION)
        self.assertEqual(len(result), 1)
        op, arg = result[0]
        self.assertEqual(op, OpCode.KOS_OP_CALL_VAR_ATTR_NAMES)
        self.assertEqual(arg, ("obj", "upper", (), 0))


# The end.
