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
Tests for help registry dispatch

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


import unittest

import pytest

import koskari.help.tools as tools
from koskari.help import (
    HelpFormatter,
    help_into,
    help_register,
    help_registry_clear,
    help_registry_snapshot,
    help_unregister,
)


pytestmark = pytest.mark.phase_infra


class TestHelpRegistry(unittest.TestCase):


    def setUp(self):
        # keep a copy to restore
        self._registry = dict(tools._registry)


    def tearDown(self):
        # restore the registry
        tools._registry = self._registry
        self._registry = None


    def test_register_and_help_into(self):
        class Reggie(object):
            pass

        def handler(obj, formatter):
            formatter.paragraph('reggie-out')

        help_register(Reggie, handler)
        fmt = HelpFormatter()
        help_into(fmt, Reggie())
        self.assertIn('reggie-out', fmt.render())


    def test_mro_wins_subclass(self):
        class Base(object):
            pass

        class Child(Base):
            pass

        help_register(Base, lambda o, f: f.paragraph('base'))
        help_register(Child, lambda o, f: f.paragraph('child'))
        fmt = HelpFormatter()
        help_into(fmt, Child())
        text = fmt.render()
        self.assertIn('child', text)
        self.assertNotIn('base', text)


    def test_unregister_restores_default(self):
        class R2(object):
            __doc__ = 'fallback doc'

        help_register(R2, lambda o, f: f.paragraph('custom'))
        help_unregister(R2)
        fmt = HelpFormatter()
        help_into(fmt, R2())
        self.assertIn('fallback doc', fmt.render())


    def test_registry_clear(self):
        class R3(object):
            pass

        help_register(R3, lambda o, f: f.paragraph('r3'))
        help_registry_clear()
        fmt = HelpFormatter()
        help_into(fmt, R3())
        self.assertEqual(fmt.render().strip(), '')


    def test_snapshot_is_shallow_copy(self):
        class Snap(object):
            pass

        help_register(Snap, lambda o, f: f.paragraph('snap'))
        snap = help_registry_snapshot()
        self.assertIn(Snap, snap)
        snap.clear()
        fmt = HelpFormatter()
        help_into(fmt, Snap())
        self.assertIn('snap', fmt.render())


# The end.
