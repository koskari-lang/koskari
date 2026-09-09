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
"""


import re
import unittest

import pytest


pytestmark = pytest.mark.phase_basics

HelpFormatter = None
help_into = None
help_register = None
help_registry_clear = None
help_registry_snapshot = None
help_unregister = None


def setUpModule():
    """
    Import koskari.help at execution time, not collection

    koskari.help is written in Koskari; loading it needs the import
    hook and the full bootstrap, which earlier phases should not pull
    in during collection.
    """

    global HelpFormatter, help_into, help_register, help_registry_clear
    global help_registry_snapshot, help_unregister

    from koskari.importer import install
    install()

    from koskari.help import (
        HelpFormatter, help_into, help_register, help_registry_clear,
        help_registry_snapshot, help_unregister)


def _plain_help(text):
    return re.sub(r'.\x08', '', text)


class TestHelpRegistry(unittest.IsolatedAsyncioTestCase):
    """
    The registry entry points are plain Koskari definitions, so Python
    callers have to await them.  The HelpFormatter methods handed to a
    handler stay demand-wrapped and are called synchronously.
    """


    async def asyncSetUp(self):
        # keep a copy to restore
        self._registry = await help_registry_snapshot()


    async def asyncTearDown(self):
        # restore the registry
        await help_registry_clear()
        for typeobj, handler in self._registry.items():
            await help_register(typeobj, handler)
        self._registry = None


    async def test_register_and_help_into(self):
        class Reggie(object):
            pass

        def handler(obj, formatter):
            formatter.paragraph('reggie-out')

        await help_register(Reggie, handler)
        fmt = HelpFormatter()
        await help_into(fmt, Reggie())
        self.assertIn('reggie-out', _plain_help(fmt.render()))


    async def test_mro_wins_subclass(self):
        class Base(object):
            pass

        class Child(Base):
            pass

        await help_register(Base, lambda o, f: f.paragraph('base'))
        await help_register(Child, lambda o, f: f.paragraph('child'))
        fmt = HelpFormatter()
        await help_into(fmt, Child())
        text = _plain_help(fmt.render())
        self.assertIn('child', text)
        self.assertNotIn('base', text)


    async def test_unregister_restores_default(self):
        class R2(object):
            """
            fallback doc
            """

        await help_register(R2, lambda o, f: f.paragraph('custom'))
        await help_unregister(R2)
        fmt = HelpFormatter()
        await help_into(fmt, R2)
        self.assertIn('fallback doc', _plain_help(fmt.render()))


    async def test_registry_clear(self):
        class R3(object):
            pass

        await help_register(R3, lambda o, f: f.paragraph('r3'))
        await help_registry_clear()
        fmt = HelpFormatter()
        await help_into(fmt, R3())
        self.assertIn('R3 object', _plain_help(fmt.render()))


    async def test_snapshot_is_shallow_copy(self):
        class Snap(object):
            pass

        await help_register(Snap, lambda o, f: f.paragraph('snap'))
        snap = await help_registry_snapshot()
        self.assertIn(Snap, snap)
        snap.clear()
        fmt = HelpFormatter()
        await help_into(fmt, Snap())
        self.assertIn('snap', _plain_help(fmt.render()))


# The end.
