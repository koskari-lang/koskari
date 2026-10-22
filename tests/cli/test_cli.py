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
import tempfile
import traceback
import unittest

import pytest

from koskari.cli import main
from koskari.cli.host import (
    REPL_INFRASTRUCTURE_MODULES, REPLCodeBoundary, run_command_async,
    run_file_async)
from koskari.exceptions import (
    CodeBoundary, _filter_infrastructure_tb, format_code_boundary_exception,
    format_exception_with_koskari_trace)
from koskari.importer import install
from koskari.module import new_module


pytestmark = pytest.mark.phase_cli


# Absolute path to the scripts directory next to this test file.
_SCRIPTS = os.path.join(os.path.dirname(__file__), 'scripts')


def _script(name):
    return os.path.join(_SCRIPTS, name)


def _launch():
    """
    Return the koskari.cli.launch submodule, importing on first use.
    """

    install()
    import koskari.cli.launch as launch
    return launch


class TestOptionParser(unittest.IsolatedAsyncioTestCase):

    async def _parse(self, *args):
        m = _launch()
        parser = await m.option_parser('koskari')
        ns = parser.parse_args(list(args))
        return await m.resolve_cli_target(parser, ns)


    async def test_defaults(self):
        opts = await self._parse()
        self.assertIsNone(opts.filename)
        self.assertIsNone(opts.module)
        self.assertEqual([], opts.args)
        self.assertTrue(opts.tweakpath)
        self.assertFalse(opts.interactive)
        self.assertFalse(opts.repl_full_trace)
        self.assertFalse(opts.quiet)
        self.assertIsNone(opts.histfile)


    async def test_repl_full_trace_flag(self):
        opts = await self._parse('--repl-full-trace')
        self.assertTrue(opts.repl_full_trace)


    async def test_filename_positional(self):
        opts = await self._parse('hello.ki')
        self.assertEqual('hello.ki', opts.filename)


    async def test_no_tweak_path(self):
        opts = await self._parse('--no-tweak-path')
        self.assertFalse(opts.tweakpath)


    async def test_interactive_short(self):
        opts = await self._parse('-i')
        self.assertTrue(opts.interactive)


    async def test_interactive_long(self):
        opts = await self._parse('--interactive')
        self.assertTrue(opts.interactive)


    async def test_histfile(self):
        opts = await self._parse('--histfile', '/tmp/koskari_hist')
        self.assertEqual('/tmp/koskari_hist', opts.histfile)


    async def test_args_remainder(self):
        opts = await self._parse('script.ki', 'arg1', 'arg2')
        self.assertEqual('script.ki', opts.filename)
        self.assertEqual(['arg1', 'arg2'], opts.args)


    async def test_compile_kio_source_name(self):
        opts = await self._parse(
            '--compile-kio',
            'out.kio',
            'x.ki',
            '--compile-kio-source-name',
            'pkg/foo.ki')
        self.assertEqual('out.kio', opts.compile_kio)
        self.assertEqual('pkg/foo.ki', opts.compile_kio_source_name)
        self.assertEqual('x.ki', opts.filename)


    async def test_command_conflicts_with_compile_kio(self):
        parser = await _launch().option_parser('koskari')
        with self.assertRaises(SystemExit):
            parser.parse_args(
                ['-c', '(print 1)', '--compile-kio', 'out.kio', 'x.ki'])


    async def test_command_conflicts_with_execute_kio(self):
        parser = await _launch().option_parser('koskari')
        with self.assertRaises(SystemExit):
            parser.parse_args(['-c', '(print 1)', '--execute-kio', 'x.kio'])


    async def test_filename_conflicts_with_compile_kio(self):
        m = _launch()
        parser = await m.option_parser('koskari')
        ns = parser.parse_args(['--compile-kio', 'out.kio', 'x.ki', 'hello.ki'])
        with self.assertRaises(SystemExit):
            await m.resolve_cli_target(parser, ns)


    async def test_misplaced_option_in_rest_errors(self):
        m = _launch()
        parser = await m.option_parser('koskari')
        ns = parser.parse_args(['hello.ki', '--compile-kio', 'out.kio', 'x.ki'])
        with self.assertRaises(SystemExit):
            await m.resolve_cli_target(parser, ns)


    async def test_quiet_flag(self):
        opts = await self._parse('-q')
        self.assertTrue(opts.quiet)


    async def test_command_short(self):
        opts = await self._parse('-c', '(print 1)')
        self.assertEqual('(print 1)', opts.command)
        self.assertIsNone(opts.filename)


    async def test_command_long(self):
        opts = await self._parse('--command', '(+ 1 2)')
        self.assertEqual('(+ 1 2)', opts.command)


    async def test_command_forwards_args(self):
        opts = await self._parse('-c', '(print 1)', 'arg1', 'arg2')
        self.assertEqual('(print 1)', opts.command)
        self.assertEqual(['arg1', 'arg2'], opts.args)


    async def test_command_strips_leading_double_dash(self):
        opts = await self._parse('-c', '(print 1)', '--', 'arg1')
        self.assertEqual('(print 1)', opts.command)
        self.assertEqual(['arg1'], opts.args)


    async def test_module_short(self):
        opts = await self._parse('-m', 'pkg.mod')
        self.assertEqual('pkg.mod', opts.module)
        self.assertIsNone(opts.filename)
        self.assertEqual([], opts.args)


    async def test_module_long_forwards_args(self):
        opts = await self._parse('--module', 'pkg.mod', 'arg1', 'arg2')
        self.assertEqual('pkg.mod', opts.module)
        self.assertEqual(['arg1', 'arg2'], opts.args)


    async def test_module_strips_leading_double_dash(self):
        opts = await self._parse('-m', 'pkg.mod', '--', '--flag', 'x')
        self.assertEqual('pkg.mod', opts.module)
        self.assertEqual(['--flag', 'x'], opts.args)


    async def test_module_conflicts_with_command(self):
        parser = await _launch().option_parser('koskari')
        with self.assertRaises(SystemExit):
            parser.parse_args(['-m', 'pkg.mod', '-c', '(print 1)'])


    async def test_script_rest_after_double_dash(self):
        opts = await self._parse('script.ki', '--', '--loops', '5')
        self.assertEqual('script.ki', opts.filename)
        self.assertEqual(['--loops', '5'], opts.args)


    async def test_script_rest_merges_pre_and_post(self):
        opts = await self._parse('script.ki', '20', '--', 'tree')
        self.assertEqual('script.ki', opts.filename)
        self.assertEqual(['20', 'tree'], opts.args)


    async def test_script_double_dash_only(self):
        opts = await self._parse('script.ki', '--')
        self.assertEqual('script.ki', opts.filename)
        self.assertEqual([], opts.args)


    async def test_misplaced_option_before_double_dash_still_errors(self):
        m = _launch()
        parser = await m.option_parser('koskari')
        ns = parser.parse_args(
            ['script.ki', '--compile-kio', 'out.kio', 'x.ki', '--', 'foo'])
        with self.assertRaises(SystemExit):
            await m.resolve_cli_target(parser, ns)


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
        self.assertIn('File "example.ki", line 12, in inner-fn', rendered)
        self.assertIn('ZeroDivisionError', rendered)


    @unittest.skipUnless(
        sys.version_info >= (3, 11),
        'PEP 657 caret columns require Python 3.11+')
    async def test_renders_source_line_and_caret_from_end_columns(self):
        import linecache
        import os
        import tempfile

        with tempfile.NamedTemporaryFile(
                'w', suffix='.ki', delete=False) as fh:
            fh.write('define foo [x]:\n    (+ 1 (bar x))\n')
            path = fh.name
        self.addCleanup(os.unlink, path)
        self.addCleanup(linecache.clearcache)

        g = {
            '__name__': 'koskari.native',
            '__koskari_trace__': [
                {
                    'name': 'foo',
                    'filename': path,
                    'line': 2,
                    'col': 9,
                    'end_line': 2,
                    'end_col': 16,
                    'pc': 2,
                },
            ],
        }

        try:
            exec('def boom():\n    1/0\nboom()\n', g, g)
        except Exception:
            exc_type, exc_val, tb = sys.exc_info()
        else:
            self.fail('expected an exception from synthetic frame test')

        rendered = ''.join(
            format_exception_with_koskari_trace(exc_type, exc_val, tb))
        self.assertIn('line 2, in foo', rendered)
        # source line pulled from the file by linecache
        self.assertIn('(+ 1 (bar x))', rendered)
        # PEP 657 caret drawn from the end-column span
        self.assertIn('^^^', rendered)


    @unittest.skipUnless(
        sys.version_info >= (3, 11),
        'PEP 657 caret columns require Python 3.11+')
    async def test_single_column_caret_when_no_end_columns(self):
        import linecache
        import os
        import tempfile

        with tempfile.NamedTemporaryFile(
                'w', suffix='.ki', delete=False) as fh:
            fh.write('define foo [x]:\n    (+ 1 (bar x))\n')
            path = fh.name
        self.addCleanup(os.unlink, path)
        self.addCleanup(linecache.clearcache)

        # start-oriented frame: col but no end_col / end_line
        g = {
            '__name__': 'koskari.native',
            '__koskari_trace__': [
                {
                    'name': 'foo',
                    'filename': path,
                    'line': 2,
                    'col': 9,
                    'pc': 2,
                },
            ],
        }

        try:
            exec('def boom():\n    1/0\nboom()\n', g, g)
        except Exception:
            exc_type, exc_val, tb = sys.exc_info()
        else:
            self.fail('expected an exception from synthetic frame test')

        rendered = ''.join(
            format_exception_with_koskari_trace(exc_type, exc_val, tb))
        self.assertIn('(+ 1 (bar x))', rendered)
        # a single-column caret is synthesized from col + 1 pointing at the
        # start of the (bar x) form; one '^', not a multi-column span
        caret_lines = [ln for ln in rendered.splitlines()
                       if ln.strip() and set(ln.strip()) == {'^'}]
        self.assertTrue(caret_lines)
        for caret in caret_lines:
            self.assertEqual(caret.count('^'), 1)


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
        self.assertIn('File "<stdin>"', rendered)
        self.assertIn('in __main__', rendered)
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


    async def test_main_script_error_prints_each_user_frame_once(self):
        """
        An uncaught script error prints the user frames once, as one
        dynchain block, and never the launchpad's own frame
        """

        def run():
            captured = io.StringIO()
            orig = sys.stderr
            sys.stderr = captured
            try:
                return (main(['koskari', _script('divide_by_zero.ki')]),
                        captured.getvalue())
            finally:
                sys.stderr = orig

        loop = asyncio.get_running_loop()
        code, err = await loop.run_in_executor(None, run)
        self.assertEqual(1, code, err)
        self.assertNotIn('launch.ki', err)
        self.assertEqual(1, err.count('Koskari dynchain'), err)
        self.assertEqual(1, err.count('line 7, in __main__'), err)
        self.assertEqual(1, err.count('line 2, in f'), err)
        self.assertTrue(err.rstrip().endswith(
            'ZeroDivisionError: division by zero'), err)


    async def test_main_run_hello_exits_zero(self):
        code, out = await self._capture_main(_script('hello.ki'))
        self.assertEqual(0, code)
        self.assertIn('Hello, koskari!', out)


    async def test_main_run_define_and_print(self):
        code, out = await self._capture_main(_script('define_and_print.ki'))
        self.assertEqual(0, code)
        self.assertIn('42', out)


    async def test_main_script_directory_on_path(self):
        """
        A script run from elsewhere imports the .ki beside it: its own
        directory is sys.path[0], as Python puts a script's directory
        there, and the entry is absolute
        """

        prev_path = sys.path[:]
        with tempfile.TemporaryDirectory() as tmp:
            sub = os.path.join(tmp, 'sub')
            os.makedirs(sub)
            with open(os.path.join(sub, 'helper.ki'), 'w') as fh:
                fh.write("define VALUE 42\n")
            with open(os.path.join(sub, 'app.ki'), 'w') as fh:
                fh.write("import sys\n"
                         "print \"path0\" sys.path[0]\n"
                         "import helper\n"
                         "print helper.VALUE\n")
            try:
                code, out = await self._capture_main(
                    os.path.join(sub, 'app.ki'))
            finally:
                sys.path[:] = prev_path
                sys.modules.pop('helper', None)
        self.assertEqual(0, code, out)
        self.assertIn(f"path0 {sub}", out)
        self.assertIn("42", out)


    async def test_main_command_has_working_directory_on_path(self):
        prev_path = sys.path[:]
        try:
            code, out = await self._capture_main(
                '-c', '(import sys) (print "path0" sys.path[0])')
        finally:
            sys.path[:] = prev_path
        self.assertEqual(0, code, out)
        self.assertIn(f"path0 {os.getcwd()}", out)


    async def _run_module_in(self, tmp, top, *argv):
        """
        Run main from tmp as the working directory, restoring the
        working directory, sys.path and sys.argv afterward and dropping
        the top-level module named top, and its submodules, from
        sys.modules.

        Returns (exit_code, stdout_text, sys.argv as the run left it).
        """

        prev_cwd = os.getcwd()
        prev_path = sys.path[:]
        prev_argv = sys.argv
        os.chdir(tmp)
        try:
            code, out = await self._capture_main(*argv)
            argv_after = list(sys.argv)
        finally:
            os.chdir(prev_cwd)
            sys.path[:] = prev_path
            sys.argv = prev_argv
            for name in list(sys.modules):
                if name == top or name.startswith(top + '.'):
                    del sys.modules[name]
        return code, out, argv_after


    async def test_main_module_runs_package_module_as_main(self):
        """
        -m finds a package's module from the working directory, runs it
        as __main__ under its package so a relative import resolves,
        and names the module's file as sys.argv[0]
        """

        with tempfile.TemporaryDirectory() as tmp:
            pkg = os.path.join(tmp, 'mpkg')
            os.makedirs(pkg)
            with open(os.path.join(pkg, '__init__.ki'), 'w') as fh:
                fh.write("define PKG_VALUE 7\n")
            with open(os.path.join(pkg, 'helper.ki'), 'w') as fh:
                fh.write("define VALUE 42\n")
            with open(os.path.join(pkg, 'app.ki'), 'w') as fh:
                fh.write("import sys\n"
                         "import-from .helper VALUE\n"
                         "print \"name\" __name__\n"
                         "print \"package\" __package__\n"
                         "print \"path0\" sys.path[0]\n"
                         "print \"value\" VALUE\n"
                         "print \"args\" (item-slice sys.argv 1)\n")
            code, out, argv = await self._run_module_in(
                tmp, 'mpkg', '-m', 'mpkg.app', '--', 'one', '--two')
            self.assertEqual(0, code, out)
            self.assertIn("name __main__", out)
            self.assertIn("package mpkg", out)
            self.assertIn(f"path0 {os.path.realpath(tmp)}", out)
            self.assertIn("value 42", out)
            self.assertIn("args ['one', '--two']", out)
            self.assertEqual(
                os.path.join(os.path.realpath(tmp), 'mpkg', 'app.ki'),
                argv[0])


    async def test_main_module_package_runs_its_main(self):
        with tempfile.TemporaryDirectory() as tmp:
            pkg = os.path.join(tmp, 'mpkg')
            os.makedirs(pkg)
            with open(os.path.join(pkg, '__init__.ki'), 'w') as fh:
                fh.write("define PKG_VALUE 7\n")
            with open(os.path.join(pkg, 'helper.ki'), 'w') as fh:
                fh.write("define VALUE 42\n")
            with open(os.path.join(pkg, '__main__.ki'), 'w') as fh:
                fh.write("import-from .helper VALUE\n"
                         "print \"main\" __name__ VALUE\n")
            code, out, _argv = await self._run_module_in(
                tmp, 'mpkg', '-m', 'mpkg')
            self.assertEqual(0, code, out)
            self.assertIn("main __main__ 42", out)


    async def test_main_module_runs_current_kio(self):
        """
        A module whose .kio is current beside its source runs from the
        .kio
        """

        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, 'kmod.ki')
            with open(src, 'w') as fh:
                fh.write("print \"file\" __file__\n")
            code, out, _argv = await self._run_module_in(
                tmp, 'kmod',
                '--compile-kio', os.path.join(tmp, 'kmod.kio'), src)
            self.assertEqual(0, code, out)
            code, out, _argv = await self._run_module_in(
                tmp, 'kmod', '-m', 'kmod')
            self.assertEqual(0, code, out)
            self.assertIn("file ", out)
            self.assertTrue(out.strip().endswith('kmod.kio'), out)


    async def test_main_module_missing_reports_and_exits_one(self):
        def run():
            captured = io.StringIO()
            orig = sys.stderr
            prev_path = sys.path[:]
            sys.stderr = captured
            try:
                return main(['koskari', '-m', 'no_such_module_here']), \
                    captured.getvalue()
            finally:
                sys.stderr = orig
                sys.path[:] = prev_path

        loop = asyncio.get_running_loop()
        code, err = await loop.run_in_executor(None, run)
        self.assertEqual(1, code)
        self.assertIn("No module named no_such_module_here", err)


    async def test_main_compile_kio_then_execute_kio_branches(self):
        """
        A program with an if/else compiles to .kio and the .kio runs
        the taken branch
        """

        with tempfile.TemporaryDirectory() as tmpdir:
            kio_path = os.path.join(tmpdir, 'if_else.kio')
            code, out = await self._capture_main(
                '--compile-kio', kio_path, _script('if_else.ki'))
            self.assertEqual(0, code)
            self.assertTrue(os.path.exists(kio_path))
            code, out = await self._capture_main('--execute-kio', kio_path)
            self.assertEqual(0, code)
            self.assertIn('one', out)
            self.assertNotIn('other', out)


    async def test_main_run_command_exits_zero(self):
        code, out = await self._capture_main('-c', '(print "Hello, koskari!")')
        self.assertEqual(0, code)
        self.assertIn('Hello, koskari!', out)


    async def test_main_command_reads_sexpr_not_layout(self):
        """
        -c reads s-expressions, so several forms on one line are
        consecutive top-level forms rather than one implicit call
        """

        code, out = await self._capture_main(
            '-c', '(print "a") (print "b")')
        self.assertEqual(0, code)
        self.assertEqual(['a', 'b'], out.split())


    async def test_main_command_pragma_keeps_sexpr(self):
        """
        A pragma form in a -c string leaves the later forms on the line
        read as s-expressions
        """

        code, out = await self._capture_main(
            '-c',
            '(pragma [compiler/tco-enable True]) (print "a") (print "b")')
        self.assertEqual(0, code)
        self.assertEqual(['a', 'b'], out.split())


    async def test_main_stdin_dash_reads_layout(self):
        """
        - runs a whole program from stdin, read as layout like a file
        """

        source = 'define greet [who]:\n  print f"hi {who}"\ngreet "stdin"\n'

        def run():
            captured = io.StringIO()
            orig_out, orig_in = sys.stdout, sys.stdin
            sys.stdout, sys.stdin = captured, io.StringIO(source)
            try:
                return main(['koskari', '-']), captured.getvalue()
            finally:
                sys.stdout, sys.stdin = orig_out, orig_in

        loop = asyncio.get_running_loop()
        code, out = await loop.run_in_executor(None, run)
        self.assertEqual(0, code)
        self.assertIn('hi stdin', out)


    async def test_main_stdin_dash_forwards_script_args(self):
        """
        - keeps its place as argv[0] with script arguments after it
        """

        def run():
            orig_argv, orig_in = sys.argv, sys.stdin
            captured = io.StringIO()
            orig_out = sys.stdout
            sys.stdout = captured
            sys.stdin = io.StringIO('import sys\nprint sys.argv\n')
            try:
                return main(['koskari', '-', 'one', 'two']), \
                    captured.getvalue()
            finally:
                sys.stdout, sys.stdin, sys.argv = \
                    orig_out, orig_in, orig_argv

        loop = asyncio.get_running_loop()
        code, out = await loop.run_in_executor(None, run)
        self.assertEqual(0, code)
        self.assertIn("['-', 'one', 'two']", out)


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


    async def test_main_repl_sets_sys_argv_like_python(self):
        """
        an interactive session has sys.argv[0] == '' as Python's does
        """

        def run():
            orig_argv, orig_stdin = sys.argv, sys.stdin
            sys.stdin = io.StringIO('')
            try:
                code = main(['koskari'])
                return code, list(sys.argv)
            finally:
                sys.argv, sys.stdin = orig_argv, orig_stdin

        loop = asyncio.get_running_loop()
        code, argv = await loop.run_in_executor(None, run)
        self.assertEqual(0, code)
        self.assertEqual([''], argv)


class TestMainEntry(unittest.IsolatedAsyncioTestCase):
    """
    A program's declare-entry function runs once its module has loaded,
    and its return value is the exit status
    """

    async def _main(self, tmp, tops, *argv, stdin=None):
        """
        Run main from tmp as the working directory, capturing stdout and
        stderr, restoring the working directory, sys.path, sys.argv and
        sys.stdin afterward, and dropping the modules named in tops,
        and their submodules, from sys.modules.

        Returns (exit_code, stdout_text, stderr_text).
        """

        def run():
            out, err = io.StringIO(), io.StringIO()
            saved = (sys.stdout, sys.stderr, sys.stdin, sys.argv)
            sys.stdout, sys.stderr = out, err
            if stdin is not None:
                sys.stdin = io.StringIO(stdin)
            try:
                code = main(['koskari'] + list(argv))
            finally:
                sys.stdout, sys.stderr, sys.stdin, sys.argv = saved
            return code, out.getvalue(), err.getvalue()

        prev_cwd = os.getcwd()
        prev_path = sys.path[:]
        os.chdir(tmp)
        try:
            loop = asyncio.get_running_loop()
            return await loop.run_in_executor(None, run)
        finally:
            os.chdir(prev_cwd)
            sys.path[:] = prev_path
            for name in list(sys.modules):
                if any(name == top or name.startswith(top + '.')
                       for top in tops):
                    del sys.modules[name]


    def _write(self, path, text):
        with open(path, 'w') as fh:
            fh.write(text)
        return path


    async def test_int_return_is_exit_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_int.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  3\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(3, code, err)


    async def test_none_return_exits_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_none.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  print \"entered\"\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(0, code, err)
        self.assertEqual("entered\n", out)


    async def test_other_return_printed_exits_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_text.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  \"bad thing\"\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(1, code, err)
        self.assertEqual("bad thing\n", err)


    async def test_no_entry_exits_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'no_entry.ki'),
                "define main []:\n"
                "  print \"never\"\n"
                "  7\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(0, code, err)
        self.assertEqual("", out)


    async def test_unbound_entry_raises(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_unbound.ki'),
                "(declare-entry missing)\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(1, code, err)
        self.assertIn("NameError", err)
        self.assertIn('"missing"', err)


    async def test_entry_runs_after_when_main(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_when_main.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  print \"entry\"\n"
                "\n"
                "when-main:\n"
                "  print \"when-main\"\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(0, code, err)
        self.assertEqual("when-main\nentry\n", out)


    async def test_entry_runs_after_seal(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_sealed.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  print \"sealed\" main.code.arena.sealed\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(0, code, err)
        self.assertEqual("sealed True\n", out)


    async def test_imported_entry_not_called(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._write(
                os.path.join(tmp, 'entry_library.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  print \"library entry\"\n")
            script = self._write(
                os.path.join(tmp, 'entry_app.ki'),
                "import entry_library\n"
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  print \"app entry\"\n")
            code, out, err = await self._main(
                tmp, ('entry_library',), script)
        self.assertEqual(0, code, err)
        self.assertEqual("app entry\n", out)


    async def test_command_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, out, err = await self._main(
                tmp, (), '-c',
                '(declare-entry main) (define main (lambda [] 6))')
        self.assertEqual(6, code, err)


    async def test_module_package_main_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            pkg = os.path.join(tmp, 'entrypkg')
            os.makedirs(pkg)
            self._write(os.path.join(pkg, '__init__.ki'), "")
            self._write(
                os.path.join(pkg, '__main__.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  5\n")
            code, out, err = await self._main(
                tmp, ('entrypkg',), '-m', 'entrypkg')
        self.assertEqual(5, code, err)


    async def test_kio_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = self._write(
                os.path.join(tmp, 'entrykio.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  4\n")
            code, out, err = await self._main(
                tmp, ('entrykio',),
                '--compile-kio', os.path.join(tmp, 'entrykio.kio'), src)
            self.assertEqual(0, code, err)
            os.remove(src)
            code, out, err = await self._main(
                tmp, ('entrykio',), '-m', 'entrykio')
        self.assertEqual(4, code, err)


    async def test_stdin_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, out, err = await self._main(
                tmp, (), '-',
                stdin="(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  8\n")
        self.assertEqual(8, code, err)


    async def test_raising_entry_exits_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_raises.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  raise (ValueError \"boom\")\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(1, code, err)
        self.assertIn("ValueError: boom", err)


    async def test_system_exit_from_entry_propagates(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_sys_exit.ki'),
                "import sys\n"
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  sys.exit 7\n")
            with self.assertRaises(SystemExit) as caught:
                await self._main(tmp, (), script)
        self.assertEqual(7, caught.exception.code)


    async def test_keyboard_interrupt_from_entry_exits_130(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_interrupt.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  raise (KeyboardInterrupt)\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(130, code, err)


    async def test_int_like_returns(self):
        cases = [("True", 1), ("False", 0), ("-1", -1), ("300", 300)]
        for value, expected in cases:
            with self.subTest(value=value):
                with tempfile.TemporaryDirectory() as tmp:
                    script = self._write(
                        os.path.join(tmp, 'entry_value.ki'),
                        "(declare-entry main)\n"
                        "\n"
                        "define main []:\n"
                        f"  {value}\n")
                    code, out, err = await self._main(tmp, (), script)
                self.assertEqual(expected, code, err)
                self.assertEqual("", err)


    async def test_non_callable_entry_exits_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_value_not_fn.ki'),
                "define main 5\n"
                "(declare-entry main)\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(1, code, err)
        self.assertIn("TypeError", err)


    async def test_interactive_raising_entry_skips_session(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_interactive_raises.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  raise (ValueError \"boom\")\n")
            code, out, err = await self._main(
                tmp, (), '-i', script, stdin='')
        self.assertEqual(1, code, err)
        self.assertIn("ValueError: boom", err)
        self.assertNotIn("Koskari ", out)


    async def test_interactive_other_return_continues_to_session(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_interactive_text.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  \"bad thing\"\n")
            code, out, err = await self._main(
                tmp, (), '-i', script, stdin='')
        self.assertEqual(0, code, err)
        self.assertIn("bad thing\n", err)
        self.assertIn("Koskari ", out)


    async def test_entry_skipped_when_load_raises(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_load_raises.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  print \"never\"\n"
                "\n"
                "raise (RuntimeError \"load failed\")\n")
            code, out, err = await self._main(tmp, (), script)
        self.assertEqual(1, code, err)
        self.assertIn("RuntimeError: load failed", err)
        self.assertEqual("", out)


    async def test_interactive_runs_entry_then_session(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = self._write(
                os.path.join(tmp, 'entry_interactive.ki'),
                "(declare-entry main)\n"
                "\n"
                "define main []:\n"
                "  print \"entered\"\n"
                "  9\n")
            code, out, err = await self._main(
                tmp, (), '-i', '-q', script, stdin='')
        self.assertEqual(0, code, err)
        self.assertIn("entered\n", out)


# The end.
