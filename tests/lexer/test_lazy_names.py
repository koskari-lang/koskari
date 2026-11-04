# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.

"""
Importing the lexer does not load the bootstrap builtins; the special
and layout name sets are built on first use

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import subprocess
import sys
import unittest

import pytest
from tests import needs_processes


pytestmark = pytest.mark.phase_infra


def run(program):
    return subprocess.run(
        [sys.executable, '-c', program],
        capture_output=True, text=True, check=True).stdout.split()


@needs_processes
class TestLazyLexerNames(unittest.TestCase):

    def test_importing_the_lexer_leaves_builtins_unloaded(self):
        out = run(
            "import sys\n"
            "import koskari.lexer\n"
            "import koskari.lexer.tokens\n"
            "print('koskari.builtins' in sys.modules)\n")
        self.assertEqual(out, ["False"])

    def test_first_use_builds_the_sets_from_builtins(self):
        out = run(
            "import sys\n"
            "import koskari.lexer\n"
            "names = koskari.lexer.SPECIAL_NAMES\n"
            "print('koskari.builtins' in sys.modules)\n"
            "print(type(names).__name__, len(names) > 0)\n"
            "print(names is koskari.lexer.tokens.SPECIAL_NAMES)\n"
            "print(koskari.lexer.LAYOUT_BLOCK_NAMES == "
            "koskari.lexer.LAYOUT_HEAD_NAMES | koskari.lexer.LAYOUT_PEER_NAMES)\n")
        self.assertEqual(out, ["True", "frozenset", "True", "True", "True"])

    def test_from_import_binds_the_built_set(self):
        out = run(
            "from koskari.lexer.tokens import SPECIAL_NAMES, tokenize\n"
            "toks = list(tokenize('(define x 1)'))\n"
            "print('define' in SPECIAL_NAMES, len(toks))\n")
        self.assertEqual(out, ["True", "5"])


# The end.
