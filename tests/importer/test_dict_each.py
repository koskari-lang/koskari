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
tests.importer.test_dict_each

dict-each collects inside the evaluator, so an await or a shift in its
value or body suspends like it does in list-each.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestDictEach(unittest.IsolatedAsyncioTestCase):
    """
    dict-each under await, shift and a colliding loop variable
    """

    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(
            source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module


    async def _eval(self, src_str):
        expr, _mod = self.compile_expr(src_str)
        return await expr()


    async def test_await_in_value(self):
        """
        an await in the value inside a coroutine suspends in place
        """

        src = """
        (begin
          (import asyncio)
          (define-coroutine f []
            (dict-each [x #[1 2]]
              key: x value: (await (asyncio.sleep 0 (* x 2)))))
          (await (f)))
        """
        self.assertEqual({1: 2, 2: 4}, await self._eval(src))


    async def test_shift_in_value(self):
        """
        a shift in the value captures up to the surrounding reset
        """

        src = """
        (reset (dict-each [x #[1 2]]
                 key: x value: (shift k (k (* x 10)))))
        """
        self.assertEqual({1: 10, 2: 20}, await self._eval(src))


    async def test_shift_in_pair_body(self):
        """
        a shift in the pair body captures up to the surrounding reset
        """

        src = """
        (reset (dict-each [x #[1 2]]
                 #(x (shift k (k (* x 10))))))
        """
        self.assertEqual({1: 10, 2: 20}, await self._eval(src))


    async def test_colliding_loop_var(self):
        """
        a loop variable named col, k or v binds the element, not the
        collector or the unpacked pair
        """

        result = await self._eval(
            '(dict-each [col #[1 2]] key: col value: (+ col 10))')
        self.assertEqual({1: 11, 2: 12}, result)

        result = await self._eval(
            '(dict-each [k #[1 2]] #(k (+ k 10)))')
        self.assertEqual({1: 11, 2: 12}, result)


    async def test_sequence_is_a_class_body_name(self):
        """
        the sequence is evaluated in the scope the form is in, so a
        class-body name is in reach
        """

        src = """
        (begin
          (define-class C []
            (define names #["a" "b"])
            (define table (dict-each [n names] key: n value: (len n))))
          C.table)
        """
        self.assertEqual({"a": 1, "b": 1}, await self._eval(src))


# The end.
