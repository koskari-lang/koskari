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
Tests for koskari.compile #bytecode special form.
"""


import asyncio
import unittest

from koskari import Module, ModuleNamespace
from koskari.parse import default_reader, source_str
from koskari.compile import Compiler, special_bytecode


def get_glbls(**defns):
    builtins = {}
    builtins.update(defns)
    return ModuleNamespace("test", builtins)


def parse_source(src_str, lineno=1):
    stream = source_str(src_str, __file__, line_no=lineno)
    return default_reader.read(stream)


def compile_expr(src_str, lineno=1, **defns):
    mod_ns = get_glbls(**defns)
    module = Module(mod_ns)
    ns = vars(module)
    r = ns.ref("#bytecode")
    r.store(special_bytecode)

    source = parse_source(src_str, lineno=lineno)
    compiler = Compiler("<unittest>", module, filename=__file__)
    fragment = compiler.full_compile(source)

    expr = fragment.as_expression(mod_ns)

    return expr, module


class TestBytecodeSpecial(unittest.TestCase):

    def test_bytecode_const_return(self):
        src_str = """
        (#bytecode
          [CONST "Hello World"]
          [RETURN 1]
        )
        """
        expr, mod = compile_expr(src_str)
        result = asyncio.run(expr())
        self.assertEqual("Hello World", result)

    def test_bytecode_var_call(self):
        src_str = """
        (#bytecode
          [VAR add]
          [VAR value_a]
          [VAR value_b]
          [CALL 2]
          [RETURN 1]
        )
        """
        expr, mod = compile_expr(
            src_str,
            add = lambda a, b: a + b,
            value_a = "Hello",
            value_b = " World",
        )
        result = asyncio.run(expr())
        self.assertEqual("Hello World", result)


# The end.
