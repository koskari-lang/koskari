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
tests.lexer.test_discovery

Unit tests for koskari.lexer.discovery.
"""

from unittest import TestCase

import pytest

from koskari.lexer.discovery import (
    collect_special_like_names_from_builtins, special_names_for_repl)


pytestmark = pytest.mark.phase_infra


class TestDiscovery(TestCase):

    def test_collect_from_builtins_includes_known_special(self):
        """
        collect_special_like_names_from_builtins finds define from basics.ki
        """

        import koskari.builtins as builtins
        bltns = vars(builtins)
        names = collect_special_like_names_from_builtins(bltns)
        self.assertNotIn('compile/define', names)
        self.assertIn('define', names)


    def test_special_names_for_repl_non_empty(self):
        names = special_names_for_repl()
        self.assertGreater(len(names), 0)
        self.assertNotIn('compile/define', names)
        self.assertIn('define', names)
        self.assertIn('import', names)


# The end.
