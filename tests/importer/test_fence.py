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
tests.importer.test_fence

Unit tests for the compile/apply-fence special and the built-in
fence/bash handler.

Runs against the full koskari builtins (import koskari.builtins), so the
bootstrap-defined fence handlers from _fences.ki are present alongside
the Python-registered ones.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest
from subprocess import CalledProcessError

import pytest

from koskari.asyncio import ProcessResult
from koskari.compile import Compiler
from koskari.exceptions import FenceError, SpecialError
from koskari.importer import install
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str
from tests import needs_processes


pytestmark = pytest.mark.phase_basics


class FenceTestCase(unittest.IsolatedAsyncioTestCase):
    """
    TestCase compiling fence sources against the full koskari builtins
    """

    def setUp(self):
        install()


    def compile_expr(self, src_str, lineno=1, **defns):
        import koskari.builtins as builtins
        bltns = vars(builtins)
        if defns:
            bltns = dict(bltns)
            bltns.update(defns)

        mod_ns = ModuleNamespace("test", bltns)
        module = Module(mod_ns)
        source = default_reader.read(source_str(src_str, __file__, line_no=lineno))
        compiler = Compiler("<unittest>", module, filename=__file__)
        fragment = compiler.compile_expression(source)
        return fragment.as_expression(mod_ns), module


class TestApplyFence(FenceTestCase):
    """
    The compile/apply-fence dispatch special
    """

    async def test_handwritten_apply_fence(self):
        # the special stands alone, independent of the reader fence syntax
        defns = {"fence/echo": lambda raw, meta: raw}
        src = '(apply-fence echo "hello")'
        expr, _mod = self.compile_expr(src, **defns)
        self.assertEqual(await expr(), "hello")


    async def test_unknown_fence(self):
        src = "```nosuch\nhello\n```\n"
        with self.assertRaises(SpecialError):
            self.compile_expr(src)


    async def test_malformed_apply_fence(self):
        # shape is enforced by the special's match patterns
        with self.assertRaises(SpecialError):
            self.compile_expr("(apply-fence)")

        with self.assertRaises(SpecialError):
            self.compile_expr("(apply-fence bash)")

        with self.assertRaises(SpecialError):
            self.compile_expr("(apply-fence bash 5)")

        with self.assertRaises(SpecialError):
            self.compile_expr('(apply-fence "bash" "raw")')


    async def test_python_callable_fence(self):
        src = "```upper\nhello\n```\n"
        defns = {"fence/upper": lambda raw, meta: raw.upper()}
        expr, _mod = self.compile_expr(src, **defns)
        self.assertEqual(await expr(), "HELLO\n")


    async def test_python_callable_fence_meta(self):
        seen = {}

        def grab(raw, meta):
            seen.update(meta)
            return raw

        src = "```grab retries: 3\nhello\n```\n"
        expr, _mod = self.compile_expr(src, **{"fence/grab": grab})
        self.assertEqual(await expr(), "hello\n")
        self.assertEqual(seen["name"], "grab")
        self.assertEqual(seen["line"], 2)
        self.assertEqual(seen["col"], 0)
        self.assertEqual(seen["options"], {"retries": 3})


    async def test_koskari_callable_fence(self):
        expr, _mod = self.compile_expr("(lambda [raw meta] (raw.upper))")
        fn = await expr()

        src = "```upper\nhello\n```\n"
        expr2, _mod2 = self.compile_expr(src, **{"fence/upper": fn})
        self.assertEqual(await expr2(), "HELLO\n")


    async def test_bare_name_is_not_a_fence(self):
        src = "```upper\nhello\n```\n"
        defns = {"upper": lambda raw, meta: raw.upper()}
        with self.assertRaises(SpecialError):
            self.compile_expr(src, **defns)


    async def test_handler_returning_non_source(self):
        src = "```bad\nhello\n```\n"
        defns = {"fence/bad": lambda raw, meta: None}
        with self.assertRaises(SpecialError):
            self.compile_expr(src, **defns)


class TestBashFence(FenceTestCase):
    """
    The built-in fence/bash handler and the run-process runtime
    """

    @needs_processes
    async def test_script_is_callable(self):
        src = "```bash\necho hello\n```\n"
        expr, _mod = self.compile_expr(src)
        script = await expr()
        self.assertTrue(callable(script))

        result = await script()
        self.assertIsInstance(result, ProcessResult)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"hello\n")
        self.assertEqual(result.text, "hello\n")


    @needs_processes
    async def test_invoked_in_expression(self):
        src = "(```bash\necho hello\n```\n)"
        expr, _mod = self.compile_expr(src)
        result = await expr()
        self.assertIsInstance(result, ProcessResult)
        self.assertEqual(result.text, "hello\n")


    @needs_processes
    async def test_args_forwarded(self):
        src = "```bash\necho \"$1:$2\"\n```\n"
        expr, _mod = self.compile_expr(src)
        script = await expr()

        result = await script("alpha", "beta")
        self.assertEqual(result.text, "alpha:beta\n")


    @needs_processes
    async def test_nonzero_exit_reported_not_raised(self):
        src = "(```bash\necho oops >&2\nexit 3\n```\n)"
        expr, _mod = self.compile_expr(src)
        result = await expr()
        self.assertIsInstance(result, ProcessResult)
        self.assertEqual(result.returncode, 3)
        self.assertEqual(result.stderr_text, "oops\n")

        with self.assertRaises(CalledProcessError) as cm:
            result.raise_for_status()
        self.assertEqual(cm.exception.returncode, 3)


    @needs_processes
    async def test_raise_for_status_chains(self):
        src = ("(.json (.raise_for_status"
               " (```bash\necho '{\"a\": 1}'\n```\n)))")
        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), {"a": 1})


    @needs_processes
    async def test_json_helper(self):
        src = "(```bash\necho '{\"a\": [1, 2]}'\n```\n)"
        expr, _mod = self.compile_expr(src)
        result = await expr()
        self.assertEqual(result.json(), {"a": [1, 2]})


    @needs_processes
    async def test_immediate_option(self):
        src = "```bash immediate: True\necho hello\n```\n"
        expr, _mod = self.compile_expr(src)
        result = await expr()
        self.assertIsInstance(result, ProcessResult)
        self.assertEqual(result.text, "hello\n")


    async def test_unknown_option_rejected(self):
        src = '```bash cwd: "/tmp"\necho hi\n```\n'
        with self.assertRaises(FenceError):
            self.compile_expr(src)


    async def test_filter_option_removed(self):
        # interpretation belongs to ProcessResult now; filter: is just
        # an unknown option
        src = "```bash filter: json\necho hi\n```\n"
        with self.assertRaises(FenceError):
            self.compile_expr(src)


# The end.
