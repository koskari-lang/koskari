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
tests.reproducers.test_compile_error_position

A compile error carries the module's file name and the line of the
innermost enclosing form, and explain() names both. The write to an
undeclared name and the local-or-global conflict carry the line of the
form that made the request, in layout mode as in s-expression mode.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.exceptions import CompilerError
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


FILENAME = "/nonexistent/compile_error_position.ki"


class TestCompileErrorPosition(unittest.IsolatedAsyncioTestCase):

    async def error_for(self, src):
        module = new_module("compile_error_position")
        vars(module)["__file__"] = FILENAME
        with self.assertRaises(CompilerError) as caught:
            await load_module_source_async(module, src)
        return caught.exception


    def assert_located(self, err, line):
        self.assertEqual(FILENAME, err.filename)
        self.assertEqual(line, err.line)
        text = err.explain()
        self.assertIn(FILENAME, text)
        self.assertIn(f":{line}", text)


    async def test_syntax_match_error(self):
        err = await self.error_for(
            "var a 1\n"
            "def g []:\n"
            "    (if)\n")
        self.assert_located(err, 3)


    async def test_unsatisfied_write_layout(self):
        err = await self.error_for(
            "var a 1\n"
            "\n"
            "def g []:\n"
            "    define q 1\n"
            "    set! undeclared 2\n"
            "    q\n")
        self.assertIn("Cannot write to 'undeclared'", str(err))
        self.assert_located(err, 5)


    async def test_unsatisfied_write_sexpr(self):
        err = await self.error_for(
            "var a 1\n"
            "\n"
            "def g []:\n"
            "    define q 1\n"
            "    (set! undeclared 2)\n"
            "    q\n")
        self.assertIn("Cannot write to 'undeclared'", str(err))
        self.assert_located(err, 5)
        self.assertEqual(10, err.column)


    async def test_local_global_conflict(self):
        err = await self.error_for(
            "var a 1\n"
            "\n"
            "def g []:\n"
            "    define q 1\n"
            "    define-global q 2\n"
            "    q\n")
        self.assertIn("cannot be both local", str(err))
        self.assert_located(err, 5)


    async def test_global_then_local_conflict(self):
        err = await self.error_for(
            "def g []:\n"
            "    define-global q 2\n"
            "\n"
            "    define q 1\n"
            "    q\n")
        self.assertIn("cannot be both local", str(err))
        self.assert_located(err, 4)


    async def test_macro_expanded_write(self):
        err = await self.error_for(
            "define-syntax bump!:\n"
            "    syntax-rules []:\n"
            "        case (bump! v):\n"
            "            (set! tmp (+ v 1))\n"
            "\n"
            "def g []:\n"
            "    define q 1\n"
            "    (bump! q)\n"
            "    q\n")
        self.assertIn("Cannot write to 'tmp'", str(err))
        self.assert_located(err, 8)


    async def test_unsupported_macro_output(self):
        err = await self.error_for(
            "define-syntax o:\n"
            "    macro-rules []:\n"
            "        case (o rest ...):\n"
            "            `(#tuple . ,rest)\n"
            "\n"
            "def f []:\n"
            "    print \"before\"\n"
            "    o 1 2\n")
        self.assertIn("Unsupported source object", str(err))
        self.assert_located(err, 8)


    async def test_unsupported_macro_output_object(self):
        err = await self.error_for(
            "define-syntax o:\n"
            "    macro-rules []:\n"
            "        case (o):\n"
            "            (object)\n"
            "\n"
            "def f []:\n"
            "    print \"before\"\n"
            "    (o)\n")
        self.assertIn("Unsupported source object", str(err))
        self.assert_located(err, 8)


    async def test_innermost_enclosing_form(self):
        err = await self.error_for(
            "def g []:\n"
            "    print:\n"
            "        1\n"
            "        (if)\n"
            "        3\n")
        self.assert_located(err, 4)


# The end.
