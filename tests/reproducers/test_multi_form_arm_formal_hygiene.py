# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or (at
# your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
tests.reproducers.test_multi_form_arm_formal_hygiene

A function a syntax-rules template defines must scope its formals and
the body's references to them together, whether the arm produces one
form or several: a symbol formal binds under the same fresh name the
body reads it by, and a keyword formal keeps the name the caller
writes while the body's scoped reference falls back to it. Binder
discovery once ran only on the direct binder path, so a define nested
under the begin that wraps a multi-form arm was missed

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import gensym, keyword, symbol
from koskari.module import load_module_source_async, new_module
from koskari.types import is_symbol, strip_mark, unpack


pytestmark = pytest.mark.phase_reproducers


SRC = """
syntax mk-kw:
  syntax-rules []:
    mk-kw name:
      var k 1

      def name [argv: None]:
        argv

syntax mk-pos:
  syntax-rules []:
    mk-pos name:
      var k 1

      def name [argv]:
        argv

syntax mk-cell:
  syntax-rules []:
    mk-cell name:
      var k 1

      def name [[argv: None]]:
        argv

var kw-expansion (macroexpand-1 '(mk-kw fa))
var pos-expansion (macroexpand-1 '(mk-pos fb))
var cell-expansion (macroexpand-1 '(mk-cell fc))

(mk-kw fa)
(mk-pos fb)
(mk-cell fc)
"""


def defined_function_parts(expansion):
    """
    The formals list and body form of the second define in a
    (begin (define k 1) (define NAME FORMALS BODY)) expansion
    """

    forms = list(unpack(expansion))
    define = list(unpack(forms[2]))
    return list(unpack(define[2])), define[3]


class TestMultiFormArmFormalHygiene(unittest.IsolatedAsyncioTestCase):

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module


    async def test_keyword_formal_scoped_with_body(self):
        mod = await self.load("mfa_kw")
        formals, body = defined_function_parts(vars(mod)["kw-expansion"])
        self.assertIs(formals[0], keyword("argv"))
        self.assertIsInstance(body, gensym)
        self.assertIs(strip_mark(body), symbol("argv"))
        self.assertNotEqual(str(body), "argv")


    async def test_keyword_formal_function_callable(self):
        mod = await self.load("mfa_kw_call")
        self.assertEqual(await vars(mod)["fa"](1), 1)
        self.assertIsNone(await vars(mod)["fa"]())


    async def test_positional_formal_renamed_with_body(self):
        mod = await self.load("mfa_pos")
        formals, body = defined_function_parts(vars(mod)["pos-expansion"])
        self.assertIsInstance(formals[0], gensym)
        self.assertTrue(is_symbol(strip_mark(formals[0])))
        self.assertEqual(formals[0], body)
        self.assertNotEqual(str(body), "argv")


    async def test_positional_formal_function_callable(self):
        mod = await self.load("mfa_pos_call")
        self.assertEqual(await vars(mod)["fb"](2), 2)



    async def test_keyword_headed_cell_formal_scoped_with_body(self):
        mod = await self.load("mfa_cell")
        formals, body = defined_function_parts(vars(mod)["cell-expansion"])
        cell = list(unpack(formals[0]))
        self.assertIs(cell[0], keyword("argv"))
        self.assertIsInstance(body, gensym)
        self.assertIs(strip_mark(body), symbol("argv"))
        self.assertNotEqual(str(body), "argv")


    async def test_keyword_headed_cell_formal_function_callable(self):
        mod = await self.load("mfa_cell_call")
        self.assertEqual(await vars(mod)["fc"](3), 3)
        self.assertIsNone(await vars(mod)["fc"]())


# The end.
