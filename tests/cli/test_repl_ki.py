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


import io
import unittest

import pytest

from koskari.cli import startup_lines
from koskari.importer import install
from koskari.module import new_module


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

    async def _run_repl(self, lines, full_trace=False, quiet=True):
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

        mod = new_module('__main__')
        await _repl().repl_async(
            mod,
            prompter=prompter,
            stdout=stdout,
            stderr=stderr,
            full_trace=full_trace,
            quiet=quiet)
        return stdout.getvalue(), stderr.getvalue()


    async def test_module_imports(self):
        self.assertTrue(hasattr(_repl(), 'repl_async'))
        self.assertTrue(hasattr(_repl(), 'make_prompter'))


    async def test_make_prompter_returns_callable(self):
        # Exercises the real backend-selection path (prompt_toolkit or the
        # readline/input fallback); both branches return an input()-style
        # callable.
        prompter = await _repl().make_prompter(get_namespace=lambda: {})
        self.assertTrue(callable(prompter))


    async def test_simple_expression_prints_result(self):
        out, _ = await self._run_repl(['(+ 1 2)'])
        self.assertIn('3', out)


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
        self.assertNotIn('_eval_expression_async', err)
        self.assertNotIn('koskari/cli', err)


# The end.
