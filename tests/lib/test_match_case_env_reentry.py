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
tests.lib.test_match_case_env_reentry

A match site whose externs all come from the defaults holds its
resolved environment while the type test runs. A type test that
changes the defaults and re-enters the same site replaces the site's
cached environment; the outer match must still complete on the one it
started with.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import get_extern_defaults, reset_extern_defaults
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_lib


SRC = """
define tagged [v]:
    match v:
        case m :: int:
            (#list "int" m)
        else:
            "other"
"""


class TestMatchCaseEnvReentry(unittest.IsolatedAsyncioTestCase):

    async def load(self):
        module = new_module("match_case_env_reentry")
        vars(module)["__file__"] = "<match_case_env_reentry>"
        await load_module_source_async(module, SRC)
        return vars(module)

    async def test_type_test_that_rebinds_defaults_and_reenters(self):
        mod = await self.load()
        tagged = mod["tagged"]
        defaults = get_extern_defaults()
        inner = []

        class Meta(type):
            def __instancecheck__(cls, obj):
                if obj == "outer" and not inner:
                    defaults["str"] = str
                    inner.append(tagged(9).demand())
                return obj == "outer" or obj == 9

        class Fake(metaclass=Meta):
            pass

        try:
            defaults["int"] = Fake
            self.assertEqual(await tagged(1), "other")
            self.assertEqual(await tagged("outer"), ["int", "outer"])
            self.assertEqual(inner, [["int", 9]])
            self.assertEqual(await tagged("outer"), ["int", "outer"])
        finally:
            reset_extern_defaults()


# The end.
