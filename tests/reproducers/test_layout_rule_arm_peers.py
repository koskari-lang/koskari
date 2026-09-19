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
tests.reproducers.test_layout_rule_arm_peers

A layout if with an else peer as the first entry of a transformer rule
arm must gather that peer. Folding the arm's entries one at a time left
the cond without an else clause and compiled the else block as a
sibling call of a variable named else

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
define-syntax sr-guard:
  syntax-rules []:
    sr-guard test body ...:
      if test:
        None
      else:
        (begin body ...)

define-syntax mr-guard:
  macro-rules []:
    mr-guard test body ...:
      if test.datum:
        None
      else:
        `(begin ,@body)

define-syntax bare-guard:
  bare-syntax me:
    if False:
      None
    else:
      "ran"

define sr-false []:
  (sr-guard False "ran")

define sr-true []:
  (sr-guard True "ran")

define mr-false []:
  (mr-guard False "ran")

define mr-true []:
  (mr-guard True "ran")

define bare-false []:
  bare-guard
"""


class TestLayoutRuleArmPeers(unittest.IsolatedAsyncioTestCase):

    async def load(self, name):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, SRC)
        return module


    async def test_syntax_rules_arm_else_runs(self):
        mod = await self.load("lrap_sr")
        self.assertEqual(await vars(mod)["sr-false"](), "ran")
        self.assertIsNone(await vars(mod)["sr-true"]())


    async def test_macro_rules_arm_else_runs(self):
        mod = await self.load("lrap_mr")
        self.assertEqual(await vars(mod)["mr-false"](), "ran")
        self.assertIsNone(await vars(mod)["mr-true"]())


    async def test_bare_syntax_else_runs(self):
        mod = await self.load("lrap_bare")
        self.assertEqual(await vars(mod)["bare-false"](), "ran")


# The end.
