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
tests.reproducers.test_kwonly_missing_stale_value

Full-stack reproducer: a lambda with two keyword-only formals and no
defaults, called with only the first supplied. The call must raise
TypeError for the missing formal. It must not bind the missing formal
to the previously bound keyword's value.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(define f (lambda [a *: nil b c] #[a b c]))
"""


class TestKwOnlyMissingStaleValue(unittest.IsolatedAsyncioTestCase):
    """
    A missing keyword-only argument raises instead of inheriting the
    previous argument's value
    """

    async def load(self, name, source):
        module = new_module(name)
        vars(module)["__file__"] = f"<{name}>"
        await load_module_source_async(module, source, layout=False)
        return module

    async def test_missing_kwonly_raises(self):
        mod = await self.load("kwonly_stale", SRC)
        f = vars(mod)["f"]
        with self.assertRaises(TypeError):
            await f(1, b=2)

    async def test_supplied_kwonly_binds(self):
        mod = await self.load("kwonly_stale_ok", SRC)
        f = vars(mod)["f"]
        self.assertEqual([1, 2, 3], await f(1, b=2, c=3))


# The end.
