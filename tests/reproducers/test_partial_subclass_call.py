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
tests.reproducers.test_partial_subclass_call

Full-stack reproducer: a Koskari class over partial that defines a
Koskari __call__ is called through that __call__, which runs in the
calling evaluator, rather than through the partial's own dispatch to
the wrapped function.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import os
import unittest

import pytest

from koskari.lib import partial
from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'partial_subclass_call.ki')


class TestPartialSubclassCall(unittest.IsolatedAsyncioTestCase):
    """
    A partial subclass with a Koskari __call__, called from Koskari
    """

    async def load_module(self):
        mod = new_module('tests.reproducers.partial_subclass_call')
        init_module(mod, os.path.abspath(_SCRIPT))
        await load_module_async(mod)
        return mod


    async def test_call_awaits_in_override(self):
        """
        The override runs and awaits inside its body
        """

        mod = await self.load_module()
        self.assertEqual(await mod.call_awaits(), ("called", 2))


    async def test_deep_recursion_through_override(self):
        """
        A deep self-recursion through the override completes or raises
        RecursionError, and never crashes
        """

        mod = await self.load_module()
        self.assertEqual(await mod.recur_deep(200), 200)
        try:
            result = await mod.recur_deep(20000)
        except RecursionError:
            pass
        else:
            self.assertEqual(result, 20000)


    async def test_tail_recursion_through_override(self):
        """
        A tail self-recursion through the override reuses the caller's
        frame at each level, so a depth no frame stack holds completes
        """

        mod = await self.load_module()
        self.assertEqual(await mod.tail_deep(200000), 200000)


    async def test_python_subclass_with_koskari_call(self):
        """
        A partial subclass built in Python with a Koskari __call__ runs
        that __call__, which awaits inside its body
        """

        mod = await self.load_module()

        class PySub(partial):
            __call__ = mod.py_call

        obj = PySub(mod.target, 1)
        self.assertEqual(await mod.call_given(obj, 2), ("py-called", 2))


# The end.
