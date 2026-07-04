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
Tests for koskari.cli: run_file_async, run_command_async, option_parser, main.

REPL behavior is covered in tests/cli/test_repl_ki.py.
"""


import asyncio
import io
import os
import sys
import traceback
import unittest

import pytest

from koskari.cli import (REPL_INFRASTRUCTURE_MODULES, REPLCodeBoundary, main,
                         option_parser, run_command_async,
                         run_file_async, _resolve_cli_target)
from koskari.exceptions import (CodeBoundary, _filter_infrastructure_tb,
                                format_code_boundary_exception,
                                format_exception_with_koskari_trace)
from koskari.module import new_module


pytestmark = pytest.mark.phase_cli


# Absolute path to the scripts directory next to this test file.
_SCRIPTS = os.path.join(os.path.dirname(__file__), 'scripts')


def _script(name):
    return os.path.join(_SCRIPTS, name)


class TestOptionParser(unittest.IsolatedAsyncioTestCase):

    def _parse(self, *args):
        parser = option_parser('koskari')
        return _resolve_cli_target(parser, parser.parse_args(list(args)))


    async def test_defaults(self):
        opts = self._parse()
        self.assertIsNone(opts.filename)
        self.assertEqual([], opts.args)
        self.assertTrue(opts.tweakpath)
        self.assertFalse(opts.interactive)
        self.assertFalse(opts.repl_full_trace)
        self.assertFalse(opts.quiet)
        self.assertIsNone(opts.histfile)


    async def test_repl_full_trace_flag(self):
        opts = self._parse('--repl-full-trace')
        self.assertTrue(opts.repl_full_trace)


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


    async def test_compile_kio_source_name(self):
        opts = self._parse(
            '--compile-kio',
            'out.kio',
            'x.ki',
            '--compile-kio-source-name',
            'pkg/foo.ki')
        self.assertEqual('out.kio', opts.compile_kio)
        self.assertEqual('pkg/foo.ki', opts.compile_kio_source_name)
        self.assertEqual('x.ki', opts.filename)


    async def test_command_conflicts_with_compile_kio(self):
        parser = option_parser('koskari')
        with self.assertRaises(SystemExit):
            parser.parse_args(['-c', '(print 1)', '--compile-kio', 'out.kio', 'x.ki'])


    async def test_command_conflicts_with_execute_kio(self):
        parser = option_parser('koskari')
        with self.assertRaises(SystemExit):
            parser.parse_args(['-c', '(print 1)', '--execute-kio', 'x.kio'])


    async def test_filename_conflicts_with_compile_kio(self):
        parser = option_parser('koskari')
        with self.assertRaises(SystemExit):
            _resolve_cli_target(
                parser,
                parser.parse_args(
                    ['--compile-kio', 'out.kio', 'x.ki', 'hello.ki']))


    async def test_misplaced_option_in_rest_errors(self):
        parser = option_parser('koskari')
        with self.assertRaises(SystemExit):
            _resolve_cli_target(
                parser,
                parser.parse_args(
                    ['hello.ki', '--compile-kio', 'out.kio', 'x.ki']))


    async def test_quiet_flag(self):
        opts = self._parse('-q')
        self.assertTrue(opts.quiet)


    async def test_command_short(self):
        opts = self._parse('-c', '(print 1)')
        self.assertEqual('(print 1)', opts.command)
        self.assertIsNone(opts.filename)


    async def test_command_long(self):
        opts = self._parse('--command', '(+ 1 2)')
        self.assertEqual('(+ 1 2)', opts.command)


    async def test_command_forwards_args(self):
        opts = self._parse('-c', '(print 1)', 'arg1', 'arg2')
        self.assertEqual('(print 1)', opts.command)
        self.assertEqual(['arg1', 'arg2'], opts.args)


    async def test_command_strips_leading_double_dash(self):
        opts = self._parse('-c', '(print 1)', '--', 'arg1')
        self.assertEqual('(print 1)', opts.command)
        self.assertEqual(['arg1'], opts.args)


    async def test_script_rest_after_double_dash(self):
        opts = self._parse('script.ki', '--', '--loops', '5')
        self.assertEqual('script.ki', opts.filename)
        self.assertEqual(['--loops', '5'], opts.args)


    async def test_script_rest_merges_pre_and_post(self):
        opts = self._parse('script.ki', '20', '--', 'tree')
        self.assertEqual('script.ki', opts.filename)
        self.assertEqual(['20', 'tree'], opts.args)


    async def test_script_double_dash_only(self):
        opts = self._parse('script.ki', '--')
        self.assertEqual('script.ki', opts.filename)
        self.assertEqual([], opts.args)


    async def test_misplaced_option_before_double_dash_still_errors(self):
        parser = option_parser('koskari')
        with self.assertRaises(SystemExit):
            _resolve_cli_target(
                parser,
                parser.parse_args(
                    ['script.ki', '--compile-kio', 'out.kio', 'x.ki', '--', 'foo']))


class TestRunFile(unittest.IsolatedAsyncioTestCase):

    async def test_hello_prints_to_stdout(self):
        mod = new_module('__main__')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await run_file_async(mod, _script('hello.ki'))
        finally:
            sys.stdout = orig
        self.assertIn('Hello, koskari!', captured.getvalue())


    async def test_define_and_print_produces_42(self):

        mod = new_module('__main__')

        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured

        try:
            await run_file_async(mod, _script('define_and_print.ki'))
        finally:
            sys.stdout = orig
        self.assertIn('42', captured.getvalue())


    async def test_run_file_raises_system_exit(self):
        mod = new_module('__main__')
        with self.assertRaises(SystemExit) as ctx:
            await run_file_async(mod, _script('exit_code.ki'))
        self.assertEqual(42, ctx.exception.code)


class TestRunCommand(unittest.IsolatedAsyncioTestCase):

    async def test_print_to_stdout(self):
        mod = new_module('__main__')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await run_command_async(mod, '(print "Hello, koskari!")')
        finally:
            sys.stdout = orig
        self.assertIn('Hello, koskari!', captured.getvalue())


    async def test_sets_string_filename(self):
        mod = new_module('__main__')
        await run_command_async(mod, '(define x 1)')
        self.assertEqual('<string>', getattr(mod, '__file__', None))


    async def test_run_command_raises_system_exit(self):
        mod = new_module('__main__')
        with self.assertRaises(SystemExit) as ctx:
            await run_command_async(mod, '(raise (SystemExit 42))')
        self.assertEqual(42, ctx.exception.code)


class TestREPLCodeBoundary(unittest.IsolatedAsyncioTestCase):

    async def test_repl_code_boundary_subclasses_code_boundary(self):
        self.assertTrue(issubclass(REPLCodeBoundary, CodeBoundary))
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
        result = _filter_infrastructure_tb(tb, REPL_INFRASTRUCTURE_MODULES)
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
        result = _filter_infrastructure_tb(tb, REPL_INFRASTRUCTURE_MODULES)
        self.assertIsNotNone(result)
        formatted = ''.join(traceback.format_exception(exc_type, exc_val, result))
        self.assertIn('ValueError', formatted)
        self.assertIn('test', formatted)


    async def test_returns_none_when_all_frames_infrastructure(self):
        g = {'__name__': 'koskari.cli'}
        try:
            exec('raise ValueError("boom")', g)
        except ValueError:
            tb = sys.exc_info()[2]
        else:
            self.fail('expected ValueError')
        # drop the test-method frame; the surviving <string> frame reports
        # module __name__ 'koskari.cli', which is infrastructure
        tb = tb.tb_next
        result = _filter_infrastructure_tb(tb, REPL_INFRASTRUCTURE_MODULES)
        self.assertIsNone(result)


class TestFormatCodeBoundaryException(unittest.IsolatedAsyncioTestCase):

    def _make_boundary_with_infra_cause_and_dynchain(self):
        g = {'__name__': 'koskari.cli'}
        try:
            exec('raise ValueError("boom")', g)
        except ValueError as e:
            cause = e
        # drop the test-method frame so every surviving cause frame is
        # infrastructure, mirroring an error raised behind the REPL await
        cause.__traceback__ = cause.__traceback__.tb_next
        cause.__koskari_dynchain__ = [
            {
                'name': '__main__',
                'filename': '<stdin>',
                'line': 0,
                'col': 0,
                'pc': 2,
            },
        ]
        try:
            raise REPLCodeBoundary() from cause
        except REPLCodeBoundary as b:
            return b


    async def test_snips_fully_infrastructure_traceback(self):
        boundary = self._make_boundary_with_infra_cause_and_dynchain()
        lines, _t, _v, tb = format_code_boundary_exception(
            boundary,
            infrastructure_modules=REPL_INFRASTRUCTURE_MODULES)
        rendered = ''.join(lines)
        self.assertNotIn('File "<string>"', rendered)
        self.assertNotIn('koskari/cli', rendered)
        self.assertIn('Koskari dynchain (native eval):', rendered)
        self.assertIn('at __main__ (<stdin>:0:0) [pc=2]', rendered)
        self.assertIn('ValueError', rendered)
        self.assertIsNone(tb)


    async def test_nofold_retains_boundary_frames(self):
        boundary = self._make_boundary_with_infra_cause_and_dynchain()
        lines, _t, _v, _tb = format_code_boundary_exception(
            boundary,
            nofold=True,
            infrastructure_modules=REPL_INFRASTRUCTURE_MODULES)
        rendered = ''.join(lines)
        self.assertIn('ValueError', rendered)
        self.assertIn('Koskari dynchain (native eval):', rendered)


class TestMain(unittest.IsolatedAsyncioTestCase):
    """
    main() uses _drive_cli_async; run it in a thread to avoid nested loop.
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


    async def test_main_run_command_exits_zero(self):
        code, out = await self._capture_main('-c', '(print "Hello, koskari!")')
        self.assertEqual(0, code)
        self.assertIn('Hello, koskari!', out)


    async def test_main_command_sets_sys_argv(self):
        def run():
            orig_argv = sys.argv
            try:
                code = main(['koskari', '-c', '(print 1)', 'extra'])
                return code, list(sys.argv)
            finally:
                sys.argv = orig_argv

        loop = asyncio.get_running_loop()
        code, argv = await loop.run_in_executor(None, run)
        self.assertEqual(0, code)
        self.assertEqual(['-c', 'extra'], argv)


    async def test_main_script_double_dash_sets_sys_argv(self):
        script = _script('hello.ki')

        def run():
            orig_argv = sys.argv
            try:
                code = main(['koskari', script, '--', '--flag'])
                return code, list(sys.argv)
            finally:
                sys.argv = orig_argv

        loop = asyncio.get_running_loop()
        code, argv = await loop.run_in_executor(None, run)
        self.assertEqual(0, code)
        self.assertEqual([script, '--flag'], argv)


    async def test_main_exit_code_script_raises_system_exit(self):
        def run():
            main(['koskari', _script('exit_code.ki')])

        loop = asyncio.get_running_loop()
        with self.assertRaises(SystemExit) as ctx:
            await loop.run_in_executor(None, run)
        self.assertEqual(42, ctx.exception.code)


    async def test_version_flag(self):
        code, out = await self._capture_main('--version')
        self.assertEqual(0, code)
        self.assertIn('koskari', out)
        self.assertIn('Python', out)


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
