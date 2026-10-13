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
tests.reproducers.test_kos_repr_koskari_method

Full-stack reproducer: a class defined in Koskari whose __kos_repr__ is
a Koskari function. kos_repr on an instance resolves the attribute to a
bound Koskari method and must call it with no arguments beyond self.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import kos_repr
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


SRC = """
(var C
  (class C ()
    (var __kos_repr__ (lambda [self] "custom repr"))))
(var inst (C))
"""


class TestKosReprKoskariMethod(unittest.IsolatedAsyncioTestCase):

    async def test_kos_repr_calls_koskari_method(self):
        module = new_module("kos_repr_method")
        vars(module)["__file__"] = "<kos_repr_method>"
        await load_module_source_async(module, SRC, layout=False)
        self.assertEqual("custom repr", kos_repr(vars(module)["inst"]))


# The end.
