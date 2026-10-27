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
tests.reproducers.test_quasiquote_bracket_build

A quasiquoted bracket list is built by the VM, as a quasiquoted plain
list is, and not by a call of the bracket-cons builtin by name: a
module that binds that name still gets bracket lists from its
quasiquotes

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import bracket_pair, symbol
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(def bracket-cons [*: a] "shadowed")
(var x 99)
(var xs #[1 2])
(var unquoted `[a ,x])
(var spliced `[a ,@xs b])
(var tailed `[a . ,x])
(var direct (bracket-cons 1 2))
(var called (call bracket-cons 1 2))
"""


class TestQuasiquoteBracketBuild(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        mod = new_module("qq_bracket_build")
        vars(mod)["__file__"] = "<qq_bracket_build>"
        await load_module_source_async(mod, SRC, layout=False)
        return vars(mod)

    async def test_unquote_builds_a_bracket_list(self):
        mod = await self.load()
        result = mod["unquoted"]
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual([symbol("a"), 99], list(result.unpack()))

    async def test_splice_builds_a_bracket_list(self):
        mod = await self.load()
        result = mod["spliced"]
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual([symbol("a"), 1, 2, symbol("b")],
                         list(result.unpack()))

    async def test_tail_builds_a_bracket_pair(self):
        mod = await self.load()
        result = mod["tailed"]
        self.assertIsInstance(result, bracket_pair)
        self.assertEqual(result.car, symbol("a"))
        self.assertEqual(result.cdr, 99)

    async def test_the_form_is_the_special_and_the_value_is_the_binding(self):
        mod = await self.load()
        direct = mod["direct"]
        self.assertIsInstance(direct, bracket_pair)
        self.assertEqual((direct.car, direct.cdr), (1, 2))
        self.assertEqual(mod["called"], "shadowed")


# The end.
