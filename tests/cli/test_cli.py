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
Tests for koskari.cli: run_file_async, repl_async, option_parser, main.
"""


import asyncio
import io
import os
import sys
import traceback
import unittest

from koskari.cli import (
    main,
    option_parser,
    repl_async,
    run_file_async,
    REPLCodeBoundary,
    format_exception_with_koskari_trace,
    _filter_infrastructure_tb,
)
from koskari.importer.module import new_module

import pytest

pytestmark = pytest.mark.phase_cli


# Absolute path to the scripts directory next to this test file.
_SCRIPTS = os.path.join(os.path.dirname(__file__), 'scripts')


def _script(name):
    return os.path.join(_SCRIPTS, name)


class TestOptionParser(unittest.IsolatedAsyncioTestCase):

    def _parse(self, *args):
        parser = option_parser('koskari')
        return parser.parse_args(list(args))


    async def test_defaults(self):
        opts = self._parse()
        self.assertIsNone(opts.filename)
        self.assertEqual([], opts.args)
        self.assertTrue(opts.tweakpath)
        self.assertFalse(opts.interactive)
        self.assertIsNone(opts.histfile)


    async def test_filename_positional(self):
        opts = self._parse('hello.ki')
        self.assertEqual('hello.ki', opts.filename)


    async def test_no_tweak_path(self):
        opts = self._parse('--no-tweak-path')
        self.assertFalse(opts.tweakpath)


    async def test_interactive_short(self):
        opts = self._parse('-i')
        self.assertTrue(opts.interactive)


    async def test_interactive_long(self):
        opts = self._parse('--interactive')
        self.assertTrue(opts.interactive)


    async def test_histfile(self):
        opts = self._parse('--histfile', '/tmp/koskari_hist')
        self.assertEqual('/tmp/koskari_hist', opts.histfile)


    async def test_args_remainder(self):
        opts = self._parse('script.ki', 'arg1', 'arg2')
        self.assertEqual('script.ki', opts.filename)
        self.assertEqual(['arg1', 'arg2'], opts.args)


class TestRunFile(unittest.IsolatedAsyncioTestCase):

    async def test_hello_prints_to_stdout(self):
        mod = await new_module('__main__')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await run_file_async(mod, _script('hello.ki'))
        finally:
            sys.stdout = orig
        self.assertIn('Hello, koskari!', captured.getvalue())


    async def test_define_and_print_produces_42(self):

        mod = await new_module('__main__')

        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured

        try:
            await run_file_async(mod, _script('define_and_print.ki'))
        finally:
            sys.stdout = orig
        self.assertIn('42', captured.getvalue())


    async def test_run_file_raises_system_exit(self):
        mod = await new_module('__main__')
        with self.assertRaises(SystemExit) as ctx:
            await run_file_async(mod, _script('exit_code.ki'))
        self.assertEqual(42, ctx.exception.code)


class TestRepl(unittest.IsolatedAsyncioTestCase):

    async def _run_repl(self, lines):
        """
        Drive the REPL with a sequence of input lines.

        Returns (stdout_text, stderr_text).
        """

        stdout = io.StringIO()
        stderr = io.StringIO()
        line_iter = iter(lines)

        def prompter(prompt):
            try:
                return next(line_iter)
            except StopIteration:
                raise EOFError

        mod = await new_module('__main__')
        await repl_async(mod, prompter=prompter, stdout=stdout, stderr=stderr)
        return stdout.getvalue(), stderr.getvalue()


    async def test_simple_expression_prints_result(self):
        out, _ = await self._run_repl(['(+ 1 2)'])
        self.assertIn('3', out)


    async def test_none_result_not_printed(self):
        out, _ = await self._run_repl(['(define x 99)'])
        self.assertNotIn('99', out)


    async def test_underscore_stores_last_result(self):
        out, _ = await self._run_repl(['(+ 3 4)', '_'])
        lines = [ln for ln in out.splitlines() if ln.strip()]
        self.assertIn('7', lines[0])
        self.assertIn('7', lines[1])


    async def test_empty_lines_ignored(self):
        out, _ = await self._run_repl(['', '   ', '(+ 1 1)'])
        self.assertIn('2', out)


    async def test_syntax_error_goes_to_stderr(self):
        _, err = await self._run_repl(['((('])
        self.assertTrue(len(err) > 0)


    async def test_runtime_error_goes_to_stderr(self):
        _, err = await self._run_repl(['(/ 1 0)'])
        self.assertTrue(len(err) > 0)


    async def test_eof_exits_cleanly(self):
        out, err = await self._run_repl([])
        self.assertEqual(0, len(err.strip()))


    async def test_multiple_expressions_accumulate_state(self):
        out, _ = await self._run_repl(['(define n 10)', '(* n n)'])
        self.assertIn('100', out)


    async def test_repl_exception_traceback_trimmed(self):
        _, err = await self._run_repl(['(/ 1 0)'])
        self.assertIn('ZeroDivisionError', err)
        self.assertNotIn(' in repl\n', err)
        self.assertNotIn(' in repl ', err)


    async def test_repl_koskari_exception_shows_native_traceback(self):
        """
        Koskari exceptions via REPL show dynchain, not wrapper traceback.
        """
        _, err = await self._run_repl(['(/ 1 0)'])

        print(err)

        self.assertIn('ZeroDivisionError', err)
        self.assertIn('Koskari dynchain', err)


class TestREPLCodeBoundary(unittest.IsolatedAsyncioTestCase):

    async def test_repl_code_boundary_exported(self):
        self.assertTrue(issubclass(REPLCodeBoundary, Exception))


class TestKoskariTracebackFormatting(unittest.IsolatedAsyncioTestCase):

    async def test_formats_koskari_dynchain_from_synthetic_frame(self):
        g = {
            '__name__': 'koskari.native',
            '__koskari_trace__': [
                {
                    'name': 'inner-fn',
                    'filename': 'example.ki',
                    'line': 12,
                    'col': 3,
                    'pc': 42,
                },
            ],
        }

        try:
            exec('def boom():\n    1/0\nboom()\n', g, g)
        except Exception:
            exc_type, exc_val, tb = sys.exc_info()
        else:
            self.fail('expected an exception from synthetic frame test')

        rendered = ''.join(format_exception_with_koskari_trace(exc_type, exc_val, tb))
        self.assertIn('Traceback (most recent call last):', rendered)
        self.assertIn('Koskari dynchain (native eval):', rendered)
        self.assertIn('at inner-fn (example.ki:12:3) [pc=42]', rendered)
        self.assertIn('ZeroDivisionError', rendered)

    async def test_falls_back_to_standard_format_without_special_frame(self):
        try:
            1 / 0
        except Exception:
            exc_type, exc_val, tb = sys.exc_info()
        else:
            self.fail('expected zero division')

        rendered = ''.join(format_exception_with_koskari_trace(exc_type, exc_val, tb))
        standard = ''.join(traceback.format_exception(exc_type, exc_val, tb))
        self.assertEqual(standard, rendered)


class TestFilterInfrastructureTb(unittest.IsolatedAsyncioTestCase):

    async def test_filters_infrastructure_frames(self):
        ns = {'__name__': 'asyncio'}
        try:
            exec('raise ValueError("test")', ns)
        except ValueError:
            tb = sys.exc_info()[2]
        else:
            self.fail('expected ValueError')
        original_count = len(list(traceback.walk_tb(tb)))
        result = _filter_infrastructure_tb(tb)
        self.assertIsNotNone(result)
        filtered_count = len(list(traceback.walk_tb(result)))
        self.assertLess(filtered_count, original_count)

    async def test_preserves_non_infrastructure_frames(self):
        ns = {'__name__': '__main__'}
        try:
            exec('raise ValueError("test")', ns)
        except ValueError:
            exc_type, exc_val, tb = sys.exc_info()
        else:
            self.fail('expected ValueError')
        result = _filter_infrastructure_tb(tb)
        self.assertIsNotNone(result)
        formatted = ''.join(traceback.format_exception(exc_type, exc_val, result))
        self.assertIn('ValueError', formatted)
        self.assertIn('test', formatted)


class TestMain(unittest.IsolatedAsyncioTestCase):
    """
    main() uses asyncio.run(); run it in a thread to avoid nested loop.
    """

    async def _capture_main(self, *argv):
        """
        Call main() with the given arguments in a thread, capturing stdout.

        Returns (exit_code, stdout_text).
        """

        def run():
            captured = io.StringIO()
            orig = sys.stdout
            sys.stdout = captured
            try:
                return main(['koskari'] + list(argv)), captured.getvalue()
            finally:
                sys.stdout = orig

        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, run)

    async def test_main_run_hello_exits_zero(self):
        code, out = await self._capture_main(_script('hello.ki'))
        self.assertEqual(0, code)
        self.assertIn('Hello, koskari!', out)

    async def test_main_run_define_and_print(self):
        code, out = await self._capture_main(_script('define_and_print.ki'))
        self.assertEqual(0, code)
        self.assertIn('42', out)

    async def test_main_exit_code_script_raises_system_exit(self):
        def run():
            main(['koskari', _script('exit_code.ki')])

        loop = asyncio.get_running_loop()
        with self.assertRaises(SystemExit) as ctx:
            await loop.run_in_executor(None, run)
        self.assertEqual(42, ctx.exception.code)

    async def test_main_no_args_runs_repl_and_exits(self):
        def run():
            orig_stdin = sys.stdin
            sys.stdin = io.StringIO('')
            try:
                return main(['koskari'])
            finally:
                sys.stdin = orig_stdin

        loop = asyncio.get_running_loop()
        code = await loop.run_in_executor(None, run)
        self.assertEqual(0, code)


# The end.
