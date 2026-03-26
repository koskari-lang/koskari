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

from koskari.lexer.discovery import (
    collect_special_like_names_from_builtins,
    special_names_for_repl,
)
from koskari.importer.bootstrap import make_bootstrap

from unittest import IsolatedAsyncioTestCase

import pytest

pytestmark = pytest.mark.phase_infra


class TestDiscovery(IsolatedAsyncioTestCase):

    async def test_collect_from_bootstrap_includes_known_special(self):
        bltns = make_bootstrap()
        names = collect_special_like_names_from_builtins(bltns)
        self.assertIn('compile/define', names)
        self.assertIn('define', names)

    async def test_special_names_for_repl_non_empty(self):
        names = special_names_for_repl()
        self.assertGreater(len(names), 0)
        self.assertIn('compile/define', names)
        self.assertIn('define', names)


# The end.
