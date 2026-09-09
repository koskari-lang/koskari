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
Tests that the koskari-written builtins load plain-heap and seal into a
single shared arena via the synthesized seal at the end of the
bootstrap loop

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest


pytestmark = pytest.mark.phase_basics


class TestBuiltinsPacked(unittest.TestCase):
    """The bootstrap .ki sources seal durable code into one arena"""

    def test_builtin_codes_packed(self):
        import koskari.builtins as builtins

        packed = []
        unpacked = []
        for name, val in vars(builtins).items():
            code = getattr(val, "code", None)
            if code is None or not hasattr(code, "arena"):
                continue
            (packed if code.arena is not None else unpacked).append(name)

        self.assertTrue(packed,
                        "no koskari-written builtins were packed")
        self.assertFalse(
            unpacked,
            f"unpacked builtin codes: {unpacked[:10]}")

        # one synthesized seal for the whole bootstrap loop: every
        # packed builtin shares a single sealed arena
        arenas = {}
        for name in packed:
            arena = vars(builtins)[name].code.arena
            arenas[id(arena)] = arena
        self.assertEqual(len(arenas), 1)
        for arena in arenas.values():
            self.assertTrue(arena.sealed)


# The end.
