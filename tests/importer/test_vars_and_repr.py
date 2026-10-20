# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Tests for (vars) with no operand, the koskari-vars builtin, and the
repr of namespaces
"""


import unittest
from textwrap import dedent

import pytest

from koskari.lib import demand_call, koskari_vars
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_basics


class VarsCase(unittest.IsolatedAsyncioTestCase):

    async def load(self, src):
        module = new_module("varsreprtest")
        ns = vars(module)
        ns['__file__'] = "<varsreprtest>"
        await load_module_source_async(module, dedent(src))
        return ns


class TestKoskariVars(VarsCase):

    async def test_tail_position_is_the_calling_frame(self):
        ns = await self.load("""
        def g [a]:
          var b 2
          (koskari-vars)
        """)
        frame = demand_call(ns['g'], 9)
        self.assertEqual(9, frame['a'])
        self.assertEqual(2, frame['b'])


    async def test_not_in_tail_position(self):
        ns = await self.load("""
        def g [a]:
          var b 2
          #((koskari-vars) 0)
        """)
        frame, _zero = demand_call(ns['g'], 9)
        self.assertEqual(9, frame['a'])


    async def test_matches_vars_special(self):
        ns = await self.load("""
        def f []:
          var k 1
          #((vars) (koskari-vars))
        """)
        special, runtime = demand_call(ns['f'])
        self.assertIs(special, runtime)


    async def test_through_apply(self):
        ns = await self.load("""
        def f []:
          var k 1
          #((vars) (apply koskari-vars #()))
        """)
        special, runtime = demand_call(ns['f'])
        self.assertIs(special, runtime)


    async def test_object_dict(self):
        ns = await self.load("""
        class K []:
          var z 1
        var d (koskari-vars K)
        """)
        self.assertEqual(1, ns['d']['z'])


    def test_object_without_dict(self):
        with self.assertRaises(TypeError):
            koskari_vars(1)


    def test_outside_evaluation(self):
        with self.assertRaises(RuntimeError):
            koskari_vars()


class TestNamespaceRepr(VarsCase):

    async def test_members_and_parent_name(self):
        ns = await self.load("""
        def f1 [a]:
          var scale 2
          (vars)
        """)
        self.assertEqual(
            "<Namespace f1 in varsreprtest: a=1 scale=2>",
            repr(demand_call(ns['f1'], 1)))


    async def test_unbound_slot(self):
        ns = await self.load("""
        def f []:
          var later
          (vars)
        """)
        self.assertEqual(
            "<Namespace f in varsreprtest: later=<unbound>>",
            repr(demand_call(ns['f'])))


    async def test_ref_slot_shows_value(self):
        ns = await self.load("""
        def f []:
          var shared 5
          def get [] shared
          #((vars) get)
        """)
        frame, _get = demand_call(ns['f'])
        self.assertIn("shared=5", repr(frame))


    async def test_empty_namespace(self):
        ns = await self.load("""
        def f []:
          (vars)
        """)
        self.assertEqual(
            "<Namespace f in varsreprtest>", repr(demand_call(ns['f'])))


    async def test_cycle_does_not_recurse(self):
        ns = await self.load("""
        def f []:
          var me None
          set! me (vars)
          me
        """)
        self.assertEqual(
            "<Namespace f in varsreprtest: me=<Namespace ...>>",
            repr(demand_call(ns['f'])))


    async def test_module_namespace(self):
        ns = await self.load("""
        var k 3
        """)
        text = repr(ns)
        self.assertTrue(text.startswith("<ModuleNamespace varsreprtest"))
        self.assertIn("k=3", text)


# The end.
