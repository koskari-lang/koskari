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
Tests for koskari.importer.basics (koskari-written builtins in basics.ki)

Placeholder phase for tests of compile/set!, future compile/if, compile/when,
compile/unless macros and other koskari-implemented builtins.
"""


import pytest
import unittest

from koskari.importer.bootstrap import make_builtins


pytestmark = pytest.mark.phase_basics


class TestBasicsModuleLoads(unittest.IsolatedAsyncioTestCase):

    async def test_basics_module_loads(self):
        """
        make_builtins loads basics.ki; compile/set! from basics.ki is present
        """
        builtins = await make_builtins()
        self.assertIn('compile/set!', builtins, 'compile/set! from basics.ki should be in builtins')


# The end.
