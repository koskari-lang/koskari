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
Tests for kos_help and Syntax help integration

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


import unittest

import pytest

from koskari.matcher import CALLEDBY, MATCH, VALUE
from koskari.syntax import special
from koskari.help import kos_help_text


pytestmark = pytest.mark.phase_infra


def _make_foo_syntax():
    """
    Build a Syntax via @special with a known docstring
    """

    pat = MATCH(CALLEDBY(), VALUE('x'))

    @special('compile/foo', patterns=pat, unpack='match')
    def handler(compiler, caps, tc):
        """
        Doc line for compile/foo
        """

        return None

    return handler


class TestKosHelp(unittest.TestCase):


    def test_kos_help_finds_compile_prefixed_binding(self):
        syn = _make_foo_syntax()
        ns = {'compile/foo': syn}
        text = kos_help_text('foo', ns)
        self.assertIn('compile/foo', text)
        self.assertIn('===========', text)
        self.assertIn('Doc line for compile/foo', text)


    def test_kos_help_uses_syntax_help(self):
        syn = _make_foo_syntax()
        ns = {'compile/foo': syn}
        text = kos_help_text('foo', ns)
        self.assertIn('1 transform arm', text)
        self.assertIn('Accepts', text)
        self.assertIn('compile/foo', text)


    def test_special_steals_doc(self):
        syn = _make_foo_syntax()
        self.assertIn('Doc line for compile/foo', syn.__doc__)


# The end.
