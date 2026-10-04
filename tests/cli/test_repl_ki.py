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
Parity tests for the koskari-native REPL (koskari.cli.repl).

Mirrors the behavioral cases in tests/cli/test_cli.py::TestRepl against the
Koskari repl_async, and guards the boundary-hiding contract from
design/cli-repl-rework.md.
"""


import asyncio
import io
import sys
import unittest

import pytest

from koskari.cli import main
from koskari.cli.host import startup_lines
from koskari.importer import install
from koskari.lib import drive_async_loop, values
from koskari.module import (
    build_module_load_facets, init_module, load_module_source_async,
    new_module)


pytestmark = pytest.mark.phase_cli


_repl_mod = None


def _repl():
    """
    Return koskari.cli.repl, importing on first use so collection does not
    import it (the module is a compiled .ki source).
    """

    # some tests may have uninstalled the importer
    install()

    global _repl_mod
    if _repl_mod is None:
        import koskari.cli.repl as repl
        _repl_mod = repl
    return _repl_mod


class TestReplKi(unittest.IsolatedAsyncioTestCase):

    async def _run_repl(self, lines, full_trace=False, quiet=True,
                        mod=None):
        """
        Drive the Koskari REPL with a sequence of input lines.

        Returns (stdout_text, stderr_text).
        """

        stdout = io.StringIO()
        stderr = io.StringIO()
        line_iter = iter(lines)

        def prompter(prompt, **kwargs):
            try:
                return next(line_iter)
            except StopIteration:
                raise EOFError

        if mod is None:
            mod = new_module('__main__')
        await _repl().repl(
            mod,
            prompter=prompter,
            stdout=stdout,
            stderr=stderr,
            full_trace=full_trace,
            quiet=quiet)
        return stdout.getvalue(), stderr.getvalue()


    async def test_module_imports(self):
        self.assertTrue(hasattr(_repl(), 'repl'))


    async def test_detached_reader_rebuilds_from_the_bound_pragma(self):
        repl = _repl()
        mod = new_module('replprobe')
        init_module(mod, '<stdin>')
        compiler, _ = build_module_load_facets(mod, filename='<stdin>')
        ns = vars(mod)
        pragma = ns['__pragma__']
        del ns['__reader__']

        await repl.eval_expression(compiler, '1', ns)

        reader = ns.get('__reader__')
        self.assertIsNotNone(reader)
        self.assertIs(ns['__pragma__'], pragma)

        # the rebuilt reader follows the module's pragma
        layout = reader.layout_enabled
        pragma.rebuild(('reader/layout', values(not layout)))
        self.assertEqual(reader.layout_enabled, not layout)


    async def test_simple_expression_prints_result(self):
        out, _ = await self._run_repl(['(+ 1 2)'])
        self.assertIn('3', out)


    async def test_prompt_reads_layout_after_a_command_load(self):
        """
        A -c string is read as s-expressions with the facets retained
        for -i; the prompt that follows still reads layout
        """

        mod = new_module('__main__')
        init_module(mod, '<string>')
        await load_module_source_async(
            mod, '(define x 41)', detach=False, layout=False)
        self.assertTrue(vars(mod)['__reader__'].layout_enabled)

        out, err = await self._run_repl(['+ x 1'], mod=mod)
        self.assertIn('42', out)
        self.assertNotIn('Error', err)


    async def test_none_result_not_printed(self):
        out, _ = await self._run_repl(['(when False 99)'])
        self.assertNotIn('99', out)


    async def test_underscore_stores_last_result(self):
        out, _ = await self._run_repl(['(+ 1 2)', '(+ _ 10)'])
        self.assertIn('13', out)


    async def test_empty_lines_ignored(self):
        out, _ = await self._run_repl(['', '  ', '(+ 1 1)'])
        self.assertIn('2', out)


    async def test_incomplete_form_continues_on_next_line(self):
        out, _ = await self._run_repl(['(+ 1', '   2)'])
        self.assertIn('3', out)


    async def test_multiple_expressions_accumulate_state(self):
        out, _ = await self._run_repl(['(define x 5)', '(* x x)'])
        self.assertIn('25', out)


    async def test_decorator_surface_enabled(self):
        """
        The REPL reader follows the pragma baseline, so @ decorator
        lines work at the prompt (reader/decorators)
        """

        out, _ = await self._run_repl(
            ['@(let [[c 41]])',
             'define bump []:',
             '    (incr! c)',
             '    c',
             '',
             '(bump)'])
        self.assertIn('42', out)


    async def test_defer_result_stays_a_handle(self):
        out, _ = await self._run_repl(
            ['(define log (list))',
             '(define f (lambda [] (log.append 1) 42))',
             '(define d (defer f))',
             '(len log)',
             '(await d)',
             '(len log)'])
        results = [line for line in out.splitlines() if line.strip()]
        self.assertEqual(results, ['0', '42', '1'])


    async def test_defer_handle_printed_and_stored(self):
        out, _ = await self._run_repl(
            ['(defer (lambda [] 42))',
             '(await _)'])
        self.assertIn('<koskari defer delegating', out)
        self.assertIn('42', out)


    async def test_spent_defer_await_raises(self):
        out, err = await self._run_repl(
            ['(define d (defer (lambda [] 42)))',
             '(await d)',
             '(await d)'])
        self.assertIn('42', out)
        self.assertIn('RuntimeError', err)
        self.assertIn('cannot reuse already awaited defer', err)


    async def test_runtime_error_goes_to_stderr(self):
        out, err = await self._run_repl(['(/ 1 0)'])
        self.assertIn('ZeroDivisionError', err)


    async def test_syntax_error_goes_to_stderr(self):
        _, err = await self._run_repl(['(+ 1 2))'])
        self.assertTrue(err.strip())


    async def test_eof_exits_cleanly(self):
        out, err = await self._run_repl([])
        # no exception; banner suppressed by quiet
        self.assertEqual('', err.strip())


    async def test_quiet_suppresses_startup_banner(self):
        out, _ = await self._run_repl([], quiet=True)
        for line in startup_lines():
            self.assertNotIn(line, out)


    async def test_startup_banner_when_not_quiet(self):
        out, _ = await self._run_repl([], quiet=False)
        for line in startup_lines():
            self.assertIn(line, out)


    async def test_boundary_hides_repl_infrastructure(self):
        # design/cli-repl-rework.md section 7: a runtime error must show the
        # native dynchain but not the REPL infrastructure frames.
        _, err = await self._run_repl(['(/ 1 0)'])
        self.assertIn('ZeroDivisionError', err)
        self.assertNotIn('_await_user_form', err)
        self.assertNotIn('koskari/cli', err)


    async def test_koskari_exception_shows_native_dynchain(self):
        # A runtime error surfaces the native dynchain, not just the Python
        # exception line.
        _, err = await self._run_repl(['(/ 1 0)'])
        self.assertIn('ZeroDivisionError', err)
        self.assertIn('Koskari dynchain', err)


    async def test_compiler_error_uses_explain_only(self):
        # Compile-phase errors render via .explain() with no Python traceback.
        form = '((lambda () (setq _repl_typo_undeclared 1)))'
        _, err = await self._run_repl([form])
        self.assertTrue(err.strip())
        self.assertNotIn('Traceback (most recent call last)', err)
        self.assertIn('not declared', err)


    async def test_full_trace_shows_boundary_frames(self):
        # --repl-full-trace (nofold) keeps the REPL boundary/infrastructure
        # frames in the rendered traceback.
        _, err = await self._run_repl(['(/ 1 0)'], full_trace=True)
        self.assertIn('ZeroDivisionError', err)
        self.assertIn('koskari.cli', err)


class TestReplKiDriver(unittest.TestCase):
    """
    Exercise the Koskari REPL under the real production async driver
    (drive_async_loop, a fresh asyncio loop), rather than
    IsolatedAsyncioTestCase, and through the main() entry point.
    """

    def _scripted_prompter(self, lines):
        line_iter = iter(lines)

        def prompter(prompt, **kwargs):
            try:
                return next(line_iter)
            except StopIteration:
                raise EOFError

        return prompter


    def test_repl_runs_under_drive_async_loop(self):
        # main() awaits the REPL inside a Python coroutine driven by
        # drive_async_loop; mirror that so loop.run_in_executor and
        # get_running_loop resolve against the production loop.
        stdout = io.StringIO()
        stderr = io.StringIO()
        prompter = self._scripted_prompter(['(+ 20 22)'])
        mod = new_module('__main__')
        repl_async = _repl().repl

        async def _driver():
            await repl_async(
                mod,
                prompter=prompter,
                stdout=stdout,
                stderr=stderr,
                quiet=True)

        drive_async_loop(_driver())
        self.assertIn('42', stdout.getvalue())


    def test_main_enters_koskari_repl(self):
        # Full stack: main() -> drive_async_loop -> cli_async -> the Koskari
        # repl_async.  Force the input() backend (no tty available) and feed
        # source on a patched stdin so no prompter is injected.
        install()
        import koskari.cli.repl  # ensure repl.ki is compiled/loaded

        saved_stdin = sys.stdin
        saved_stdout = sys.stdout
        saved_stderr = sys.stderr
        saved_pt = sys.modules.get('prompt_toolkit', False)
        try:
            sys.modules['prompt_toolkit'] = None  # force ImportError fallback
            sys.stdin = io.StringIO('(+ 20 22)\n')
            sys.stdout = io.StringIO()
            sys.stderr = io.StringIO()
            rc = main(['koskari', '-q', '--no-tweak-path'])
            out = sys.stdout.getvalue()
        finally:
            sys.stdin = saved_stdin
            sys.stdout = saved_stdout
            sys.stderr = saved_stderr
            if saved_pt is False:
                sys.modules.pop('prompt_toolkit', None)
            else:
                sys.modules['prompt_toolkit'] = saved_pt

        self.assertEqual(0, rc)
        self.assertIn('42', out)


    def test_prompt_toolkit_cooperative_on_our_loop(self):
        # The real prompt_toolkit backend, driven cooperatively on our own
        # event loop via prompt_async (no worker thread, no nested
        # asyncio.run).  A prompt_toolkit pipe input feeds one form and then
        # Ctrl-D (EOF); the EOF is delivered back to the Koskari loop's
        # except handler at the await point.
        try:
            from prompt_toolkit.application import create_app_session
            from prompt_toolkit.input.defaults import create_pipe_input
            from prompt_toolkit.output import DummyOutput
        except ImportError:
            self.skipTest('prompt_toolkit not installed')

        stdout = io.StringIO()
        stderr = io.StringIO()
        mod = new_module('__main__')
        repl_async = _repl().repl

        async def run_repl():
            # prompter=None -> repl builds the real prompt_async backend
            await repl_async(mod, stdout=stdout, stderr=stderr, quiet=True)

        async def _driver():
            # safety net so a stuck prompt fails the test instead of hanging
            await asyncio.wait_for(run_repl(), timeout=10)

        with create_pipe_input() as pipe:
            pipe.send_text('(+ 40 2)\n\x04')  # one form, then Ctrl-D (EOF)
            with create_app_session(input=pipe, output=DummyOutput()):
                drive_async_loop(_driver())

        self.assertIn('42', stdout.getvalue())


# The end.
