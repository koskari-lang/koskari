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
fence/json and fence/bash handlers.

Runs against the full koskari builtins (import koskari.builtins), so the
bootstrap-defined fence handlers from _fences.ki are present alongside
the Python-registered ones.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from subprocess import CalledProcessError

from koskari.compile import Compiler
from koskari.exceptions import FenceError, SpecialError
from koskari.lib import Module, ModuleNamespace
from koskari.importer import install
from koskari.reader import default_reader, source_str

import pytest
import unittest


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
        fragment = compiler.full_compile(source)
        return fragment.as_expression(mod_ns), module


class TestApplyFence(FenceTestCase):
    """
    The compile/apply-fence dispatch special
    """

    async def test_handwritten_apply_fence(self):
        src = '(compile/apply-fence json "{\\"a\\": 1}")'
        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), {"a": 1})


    async def test_unknown_fence(self):
        src = "```nosuch\nhello\n```\n"
        with self.assertRaises(SpecialError):
            self.compile_expr(src)


    async def test_malformed_apply_fence(self):
        # shape is enforced by the special's match patterns
        with self.assertRaises(SpecialError):
            self.compile_expr("(compile/apply-fence)")

        with self.assertRaises(SpecialError):
            self.compile_expr("(compile/apply-fence json)")

        with self.assertRaises(SpecialError):
            self.compile_expr("(compile/apply-fence json 5)")

        with self.assertRaises(SpecialError):
            self.compile_expr('(compile/apply-fence "json" "raw")')


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


class TestJsonFence(FenceTestCase):
    """
    The built-in fence/json handler
    """

    async def test_object(self):
        src = '```json\n{"a": 1, "b": "x", "c": true}\n```\n'
        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), {"a": 1, "b": "x", "c": True})


    async def test_nested(self):
        src = '```json\n{"a": {"b": [1, {"c": 2}], "d": null}}\n```\n'
        expr, _mod = self.compile_expr(src)
        self.assertEqual(
            await expr(), {"a": {"b": [1, {"c": 2}], "d": None}})


    async def test_array(self):
        src = "```json\n[1, 2.5, -3e2, true, false, null, \"x\"]\n```\n"
        expr, _mod = self.compile_expr(src)
        self.assertEqual(
            await expr(), [1, 2.5, -300.0, True, False, None, "x"])


    async def test_empty_collections(self):
        src = "```json\n{}\n```\n"
        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), {})

        src = "```json\n[]\n```\n"
        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), [])


    async def test_bare_identifier_value(self):
        src = '```json\n{"host": server-host, "port": port}\n```\n'
        defns = {"server-host": "example.org", "port": 8080}
        expr, _mod = self.compile_expr(src, **defns)
        self.assertEqual(
            await expr(), {"host": "example.org", "port": 8080})


    async def test_bare_key_rejected(self):
        src = '```json\n{ foo: 1 }\n```\n'
        with self.assertRaises(FenceError) as cm:
            self.compile_expr(src)
        self.assertEqual(cm.exception.line, 2)


    async def test_expression_value_rejected(self):
        src = '```json\n{"a": (+ 1 2)}\n```\n'
        with self.assertRaises(FenceError):
            self.compile_expr(src)


    async def test_dotted_identifier_rejected(self):
        src = '```json\n{"a": config.user}\n```\n'
        with self.assertRaises(FenceError):
            self.compile_expr(src)


    async def test_trailing_comma_rejected(self):
        src = '```json\n{"a": 1,}\n```\n'
        with self.assertRaises(FenceError):
            self.compile_expr(src)

        src = "```json\n[1, 2,]\n```\n"
        with self.assertRaises(FenceError):
            self.compile_expr(src)


    async def test_unterminated_rejected(self):
        src = '```json\n{"a": 1\n```\n'
        with self.assertRaises(FenceError):
            self.compile_expr(src)


    async def test_trailing_content_rejected(self):
        src = '```json\n{"a": 1} extra\n```\n'
        with self.assertRaises(FenceError):
            self.compile_expr(src)


    async def test_error_position_remapped(self):
        src = '```json\n{"a": 1,\n "b": (bad)}\n```\n'
        with self.assertRaises(FenceError) as cm:
            self.compile_expr(src)
        self.assertEqual(cm.exception.line, 3)


    async def test_options_rejected(self):
        src = "```json strict: True\n{}\n```\n"
        with self.assertRaises(FenceError):
            self.compile_expr(src)


class TestBashFence(FenceTestCase):
    """
    The built-in fence/bash handler and the koskari.fences.bash runtime
    """

    async def test_echo(self):
        src = "(```bash\necho hello\n```\n)"
        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), "hello\n")


    async def test_script_is_callable(self):
        src = "```bash\necho hello\n```\n"
        expr, _mod = self.compile_expr(src)
        script = await expr()
        self.assertTrue(callable(script))
        self.assertEqual(await script(), "hello\n")


    async def test_filter_json(self):
        src = "(```bash filter: json\necho '{\"a\": 1}'\n```\n)"
        expr, _mod = self.compile_expr(src)
        self.assertEqual(await expr(), {"a": 1})


    async def test_nonzero_exit(self):
        src = "(```bash\nexit 3\n```\n)"
        expr, _mod = self.compile_expr(src)
        with self.assertRaises(CalledProcessError) as cm:
            await expr()
        self.assertEqual(cm.exception.returncode, 3)


    async def test_unknown_option_rejected(self):
        src = '```bash cwd: "/tmp"\necho hi\n```\n'
        with self.assertRaises(FenceError):
            self.compile_expr(src)


# The end.
