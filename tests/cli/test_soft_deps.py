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
tests.cli.test_soft_deps

Verifies the CLI works when soft (extra) dependencies are absent.
prompt_toolkit is only required for the enhanced REPL prompt; the CLI
must import, execute commands, and fall back to the plain input()
prompter without it.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import subprocess
import sys
import unittest

import pytest
from tests import needs_processes


pytestmark = pytest.mark.phase_cli


# Run in a subprocess so the block applies before any koskari import;
# the test process itself may already have prompt_toolkit loaded.
_BLOCK_PT = """
import sys

class BlockPT:
    def find_spec(self, name, path=None, target=None):
        if name == 'prompt_toolkit' or name.startswith('prompt_toolkit.'):
            raise ModuleNotFoundError('No module named %r' % (name,))
        return None

sys.meta_path.insert(0, BlockPT())
"""


@needs_processes
class TestWithoutPromptToolkit(unittest.TestCase):

    def _run(self, program):
        return subprocess.run(
            [sys.executable, '-c', _BLOCK_PT + program],
            check=False,
            capture_output=True,
            text=True,
        )


    def test_cli_command(self):
        proc = self._run(
            'from koskari.cli import main\n'
            'sys.exit(main(["koskari", "-c", \'(print "soft-dep-ok")\']))\n')
        self.assertEqual(0, proc.returncode, proc.stderr)
        self.assertIn('soft-dep-ok', proc.stdout)


    def test_prompter_falls_back_to_input(self):
        proc = self._run(
            'from koskari.cli.host import _make_repl_prompter\n'
            'print(_make_repl_prompter(None, {}).__name__)\n')
        self.assertEqual(0, proc.returncode, proc.stderr)
        self.assertEqual('_input_prompt', proc.stdout.strip())


    def test_input_prompter_prefills_the_default_through_readline(self):
        # the continuation indent the REPL passes as default reaches the
        # input line through readline's startup hook, which is cleared
        # again once the line is read
        proc = self._run(
            'import asyncio, builtins, readline\n'
            'calls = []\n'
            'readline.set_startup_hook = lambda h=None: calls.append(("hook", h))\n'
            'readline.insert_text = lambda s: calls.append(("insert", s))\n'
            'builtins.input = lambda p: (calls.append(("input", p)), "typed")[1]\n'
            'from koskari.cli.host import _make_repl_prompter\n'
            'prompter = _make_repl_prompter(None, {})\n'
            'got = asyncio.run(prompter("... ", default="  "))\n'
            'hooks = [h for kind, h in calls if kind == "hook"]\n'
            'hooks[0]()\n'
            'print(repr(got))\n'
            'print(repr([c for c in calls if c[0] != "hook"]))\n'
            'print(repr([h is None for h in hooks]))\n')
        self.assertEqual(0, proc.returncode, proc.stderr)
        got, calls, cleared = proc.stdout.strip().splitlines()
        self.assertEqual("'typed'", got)
        self.assertEqual("[('input', '... '), ('insert', '  ')]", calls)
        self.assertEqual("[False, True]", cleared)


# The end.
