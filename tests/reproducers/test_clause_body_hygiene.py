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
tests.reproducers.test_clause_body_hygiene

A syntax-rules template that defines a function inside a cond, match,
try, or when clause must scope the function's formals and the body's
references to them together: a symbol formal and its reference carry
the same marks, and a keyword formal keeps the name the caller writes
while the body's scoped reference falls back to it. The clause bodies
of those specials were captured with REST, which registers no hygiene
region, so binder discovery never descended into them. A nested macro
whose plan holds only a repeat edge blocked discovery the same way

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import keyword, symbol
from koskari.module import load_module_source_async, new_module
from koskari.types import (
    is_mark, is_pair, is_symbol, strip_mark, unpack)


pytestmark = pytest.mark.phase_reproducers


SRC = """
define-syntax mk-cond:
  syntax-rules []:
    mk-cond name:
      cond:
        [False None]
        [else: (define name [argv: None] argv)]

define-syntax mk-match:
  syntax-rules []:
    mk-match name:
      match 1:
        case 1:
          define name [argv: None]:
            argv

define-syntax mk-try:
  syntax-rules []:
    mk-try name:
      try:
        (raise (ValueError "x"))
      except [e ValueError]:
        define name [argv: None]:
          argv

define-syntax mk-when:
  syntax-rules []:
    mk-when name:
      when True:
        define name [argv: None]:
          argv

define-syntax mk-pos:
  syntax-rules []:
    mk-pos name:
      cond:
        [else: (define name [argv] argv)]

define cond-expansion (macroexpand-1 '(mk-cond fa))
define match-expansion (macroexpand-1 '(mk-match fb))
define try-expansion (macroexpand-1 '(mk-try fc))
define when-expansion (macroexpand-1 '(mk-when fe))
define pos-expansion (macroexpand-1 '(mk-pos fd))

(mk-cond fa)
(mk-match fb)
(mk-try fc)
(mk-when fe)
(mk-pos fd)
"""


_define = symbol("define")


def find_define(form):
    """
    The first (define NAME FORMALS BODY) form in a pair tree, as its
    (formals list, body form) parts
    """

    if not is_pair(form):
        return None
    items = list(unpack(form))
    if items and strip_mark(items[0]) is _define:
        return list(unpack(items[2])), items[3]
    for item in items:
        found = find_define(item)
        if found is not None:
            return found
    return None


class TestClauseBodyHygiene(unittest.IsolatedAsyncioTestCase):

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module


    def assert_keyword_formal_scoped(self, expansion):
        formals, body = find_define(expansion)
        self.assertIs(formals[0], keyword("argv"))
        self.assertTrue(is_mark(body))
        self.assertIs(strip_mark(body), symbol("argv"))
        self.assertNotEqual(str(body), "argv")


    async def test_cond_clause_scopes_keyword_formal(self):
        mod = await self.load("cbh_cond")
        self.assert_keyword_formal_scoped(vars(mod)["cond-expansion"])
        self.assertEqual(await vars(mod)["fa"](1), 1)


    async def test_match_clause_scopes_keyword_formal(self):
        mod = await self.load("cbh_match")
        self.assert_keyword_formal_scoped(vars(mod)["match-expansion"])
        self.assertEqual(await vars(mod)["fb"](2), 2)


    async def test_try_clause_scopes_keyword_formal(self):
        mod = await self.load("cbh_try")
        self.assert_keyword_formal_scoped(vars(mod)["try-expansion"])
        self.assertEqual(await vars(mod)["fc"](3), 3)


    async def test_when_macro_scopes_keyword_formal(self):
        """
        when is a syntax-rules macro with an ellipsis; its plan has a
        repeat edge and nothing to walk, and must not stop discovery
        """

        mod = await self.load("cbh_when")
        self.assert_keyword_formal_scoped(vars(mod)["when-expansion"])
        self.assertEqual(await vars(mod)["fe"](5), 5)


    async def test_cond_clause_renames_positional_formal(self):
        mod = await self.load("cbh_pos")
        formals, body = find_define(vars(mod)["pos-expansion"])
        self.assertTrue(is_mark(formals[0]))
        self.assertTrue(is_symbol(strip_mark(formals[0])))
        self.assertEqual(formals[0], body)
        self.assertNotEqual(str(body), "argv")
        self.assertEqual(await vars(mod)["fd"](4), 4)


# The end.
