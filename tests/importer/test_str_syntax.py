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
tests.importer.test_str_syntax

The str syntax: a one-argument call lowers to OBJECT_STR and agrees
with str(), no argument is the empty string, other arities reach the
runtime builtin, and str as a value is still the type.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import OpCode
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_basics


SRC = """
(define one (lambda [x] (str x)))
(define none (lambda [] (str)))
(define decode (lambda [b] (str b "utf-8")))
(define decode_kw (lambda [b] (str b encoding: "ascii")))
(define as_value (lambda [xs] (list (map str xs))))
(define is_str (lambda [x] (instance? x str)))
"""


def ops_of(fn):
    code = fn.code
    return [OpCode(code.oparg(i)[0]) for i in range(code.length())]


class Loud(str):
    def __str__(self):
        return "loud:" + str.__str__(self)


class TestStrSyntax(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("str_syntax")
        vars(module)["__file__"] = "<str_syntax>"
        await load_module_source_async(module, SRC, layout=False)
        return vars(module)

    async def test_one_argument_lowers_to_object_str(self):
        mod = await self.load()
        ops = ops_of(mod["one"])
        self.assertIn(OpCode.OBJECT_STR, ops)
        self.assertNotIn(OpCode.CALL_FAST, ops)
        self.assertNotIn(OpCode.CALL, ops)

    async def test_one_argument_agrees_with_str(self):
        mod = await self.load()
        one = mod["one"]
        for value in (42, 2.5, None, True, [1, 2], "plain"):
            with self.subTest(value=value):
                self.assertEqual(await one(value), str(value))
        plain = "plain"
        self.assertIs(await one(plain), plain)
        self.assertEqual(await one(Loud("x")), "loud:x")
        self.assertIs(type(await one(Loud("x"))), str)

    async def test_no_argument_is_the_empty_string(self):
        mod = await self.load()
        self.assertEqual(await mod["none"](), "")
        self.assertNotIn(OpCode.OBJECT_STR, ops_of(mod["none"]))

    async def test_other_arities_reach_the_builtin(self):
        mod = await self.load()
        self.assertEqual(await mod["decode"](b"caf\xc3\xa9"), "caf\xe9")
        self.assertEqual(await mod["decode_kw"](b"abc"), "abc")
        with self.assertRaises(UnicodeDecodeError):
            await mod["decode_kw"](b"caf\xc3\xa9")

    async def test_str_as_a_value_is_the_type(self):
        mod = await self.load()
        self.assertEqual(await mod["as_value"]([1, 2.5]), ["1", "2.5"])
        self.assertTrue(await mod["is_str"]("s"))
        self.assertFalse(await mod["is_str"](5))


# The end.
