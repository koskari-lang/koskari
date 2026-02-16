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
Tests for koskari.compile lambda special (named and unnamed).
"""


import asyncio
import unittest

from koskari.compile import Compiler
from koskari.compile.specials import SPECIALS
from koskari.parse import default_reader, source_str
from koskari.types import Module, ModuleNamespace


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
    for sym, fn in SPECIALS.items():
        name = str(sym)
        if not name.startswith("compile/"):
            name = "compile/" + name
        r = ns.ref(name)
        r.store(fn)

    source = parse_source(src_str, lineno=lineno)
    compiler = Compiler("<unittest>", module, filename=__file__)
    fragment = compiler.full_compile(source)
    expr = fragment.as_expression(mod_ns)
    return expr, module


class TestLambdaAnonymous(unittest.TestCase):

    def test_anonymous_lambda_identity(self):
        src = "(lambda [x] x)"
        expr, mod = compile_expr(src)
        fn = asyncio.run(expr())
        result = asyncio.run(fn(42))
        self.assertEqual(result, 42)

    def test_anonymous_lambda_repr_has_debug_label(self):
        src = "(lambda [x] x)"
        expr, mod = compile_expr(src)
        fn = asyncio.run(expr())
        repr_str = repr(fn)
        self.assertIn("<lambda>", repr_str,
                      "anonymous lambda repr should show debug label")


class TestLambdaNamed(unittest.TestCase):

    def test_named_lambda_repr_shows_name(self):
        src = "(lambda fact [n] n)"
        expr, mod = compile_expr(src)
        fn = asyncio.run(expr())
        repr_str = repr(fn)
        self.assertIn("fact", repr_str,
                      "named lambda repr should show proper name")

    def test_named_lambda_identity(self):
        src = "(lambda fact [n] n)"
        expr, mod = compile_expr(src)
        fn = asyncio.run(expr())
        result = asyncio.run(fn(10))
        self.assertEqual(result, 10)


# The end.
