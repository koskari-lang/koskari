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
tests.reproducers.test_toplevel_position

A runtime error raised by a top-level form reports that form's line
in the Koskari traceback: a bare symbol, which carries no position of
its own, a macro call whose expansion is built without one, and a
class whose base is unbound.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


FILENAME = "/nonexistent/toplevel_position.ki"


def dynchain_lines(exc):
    frames = getattr(exc, "__koskari_dynchain__", None) or ()
    return [f.get("line") for f in frames if isinstance(f, dict)]


class TestTopLevelPosition(unittest.IsolatedAsyncioTestCase):

    async def error_for(self, src, exc_type):
        module = new_module("toplevel_position")
        vars(module)["__file__"] = FILENAME
        with self.assertRaises(exc_type) as caught:
            await load_module_source_async(module, src)
        return caught.exception


    async def test_bare_symbol(self):
        err = await self.error_for(
            "define x 6\n"
            "\n"
            "nope\n", NameError)
        self.assertEqual([3], sorted(set(dynchain_lines(err))))


    async def test_macro_call(self):
        err = await self.error_for(
            "define x 6\n"
            "\n"
            "incr! x \"s\"\n", TypeError)
        self.assertEqual([3], sorted(set(dynchain_lines(err))))


    async def test_define_class_unbound_base(self):
        err = await self.error_for(
            "define x 6\n"
            "\n"
            "class C (Base):\n"
            "    pass\n", NameError)
        self.assertEqual([3], sorted(set(dynchain_lines(err))))


# The end.
