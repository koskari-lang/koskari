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
syntax-rules templates keep their bracket delimiters through expansion.

Requires import koskari.builtins so the if / cond macros are loaded.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace, kos_repr
from koskari.reader import default_reader, source_str

pytestmark = pytest.mark.phase_basics


class TestMacroexpandBracket(unittest.IsolatedAsyncioTestCase):
    """A [ ] template survives macroexpansion as a bracket list"""

    async def eval_src(self, src_str):
        import koskari.builtins as builtins
        mod_ns = ModuleNamespace("test", vars(builtins))
        module = Module(mod_ns)
        compiler = Compiler("<unittest>", module, filename=__file__)
        # macroexpand-1 reaches the compiler through __compiler__ (bound at
        # load time by koskari.module)
        mod_ns['__compiler__'] = compiler
        source = default_reader.read(source_str(src_str, __file__))
        expr = compiler.full_compile(source).as_expression(mod_ns)
        return await expr()

    async def test_if_expands_with_brackets(self):
        """(if foo 100 200) expands to (cond [foo 100] [else: 200])"""

        result = await self.eval_src("(macroexpand-1 '(if foo 100 200))")
        self.assertEqual(
            kos_repr(result),
            "(cond [foo 100] [else: 200])")


# The end.
