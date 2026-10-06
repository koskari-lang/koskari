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
tests.reproducers.test_source_read_whole

A source file is read whole before any of it runs. The file is closed
by the time the module body executes, so text appended to it while it
runs is not executed, and a lone carriage return between forms is a
newline, as it is for a string source.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import os
import tempfile
import unittest

import pytest

from koskari.module import load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers


class TestSourceReadWhole(unittest.IsolatedAsyncioTestCase):

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)


    def write(self, name, data):
        path = os.path.join(self.tmp.name, name)
        with open(path, "wb") as f:
            f.write(data)
        return path


    async def load(self, name, path):
        module = new_module(name)
        vars(module)["__file__"] = path
        await load_module_async(module)
        return vars(module)


    async def test_text_appended_while_running_does_not_run(self):
        path = self.write("self_append.ki", (
            b"import builtins\n"
            b"define seen #[]\n"
            b"(seen.append 1)\n"
            b"((attr (builtins.open __file__ \"a\") write)"
            b" \"\\n(seen.append 99)\\n\")\n"
            b"(seen.append 2)\n"))
        m = await self.load("self_append", path)
        self.assertEqual([1, 2], m["seen"])


    async def test_lone_carriage_return_separates_forms(self):
        path = self.write("lone_cr.ki", b'(define a 1)\r(define b 2)\n')
        m = await self.load("lone_cr", path)
        self.assertEqual((1, 2), (m["a"], m["b"]))


# The end.
