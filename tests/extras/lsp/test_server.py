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
tests.extras.lsp.test_server

Unit tests for koskari.extras.lsp server.
"""


from unittest import IsolatedAsyncioTestCase
from koskari.extras.lsp import create_server

import pytest

pytestmark = pytest.mark.phase_extras


class TestCreateServer(IsolatedAsyncioTestCase):

    async def test_returns_server(self):
        server = create_server()
        self.assertIsNotNone(server)


    async def test_server_has_diagnostics_attr(self):
        server = create_server()
        self.assertTrue(hasattr(server, 'diagnostics'))




# The end.
