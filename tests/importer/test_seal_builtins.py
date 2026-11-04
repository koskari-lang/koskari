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
Tests that the koskari-written builtins' function code is placed into
arena chunks during the bootstrap loop and sealed by the synthesized
seal at its end

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import build_flags


pytestmark = pytest.mark.phase_basics


needs_protection = unittest.skipIf(
    build_flags['KOS_NO_PROTECT'],
    "built without read-only code protection")


@needs_protection
class TestBuiltinsPacked(unittest.TestCase):
    """The bootstrap .ki sources place function code into sealed chunks"""

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
        # chunk holding a builtin is sealed
        for name in packed:
            self.assertTrue(vars(builtins)[name].code.arena.sealed, name)


# The end.
