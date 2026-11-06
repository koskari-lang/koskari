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
tests.importer.test_zip_builtin

The bare zip builtin called from Koskari source, including its strict
keyword

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class TestZipBuiltin(unittest.IsolatedAsyncioTestCase):
    """
    zip as Koskari source reaches it
    """

    def compile_expr(self, src_str, lineno=1):
        import koskari.builtins as builtins

        mod_ns = ModuleNamespace("test", vars(builtins))
        module = Module(mod_ns)
        source = default_reader.read(
            source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module


    async def _eval(self, src_str):
        expr, _mod = self.compile_expr(src_str)
        return await expr()


    async def test_zip_pairs_positionally(self):
        """
        the bare zip pairs its sources
        """

        result = await self._eval('(list (zip #[1 2] #[3 4]))')
        self.assertEqual([(1, 3), (2, 4)], result)


    async def test_strict_zip_refuses_unequal_sources(self):
        """
        strict: True raises ValueError when a source ends early
        """

        with self.assertRaises(ValueError) as cm:
            await self._eval('(list (zip #[1 2] #[1] strict: True))')
        self.assertEqual("zip() argument 2 is shorter than argument 1",
                         str(cm.exception))


# The end.
