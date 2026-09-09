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
The bracket_calls compiler flag: ignore / warn / error on a bracket-pair
in call position.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.bootstrap import make_bootstrap
from koskari.compile import Compiler
from koskari.compile.pragma import BracketCalls
from koskari.exceptions import CompilerError
from koskari.lib import Module, ModuleNamespace
from koskari.reader import Reader, source_str


pytestmark = pytest.mark.phase_compile


class TestBracketCallLint(unittest.TestCase):
    """bracket_calls gates compiling a [ ] form in operator position"""

    def compile_src(self, src, bracket_calls):
        mod_ns = ModuleNamespace("test", dict(make_bootstrap()))
        module = Module(mod_ns)
        compiler = Compiler("<unittest>", module, filename=__file__,
                            bracket_calls=bracket_calls)
        source = Reader().read(source_str(src, "<unittest>"))
        return compiler.full_compile(source)

    def test_ignore_compiles(self):
        """default IGNORE: a [ ] call compiles like a paren call"""

        self.compile_src("[foo bar]", BracketCalls.IGNORE)

    def test_error_raises(self):
        """ERROR: a [ ] call raises a CompilerError"""

        with self.assertRaises(CompilerError):
            self.compile_src("[foo bar]", BracketCalls.ERROR)

    def test_warn_warns_but_compiles(self):
        """WARN: a [ ] call warns, attributed to the source, not compiler.py"""

        with self.assertWarns(SyntaxWarning) as cm:
            self.compile_src("[foo bar]", BracketCalls.WARN)
        # attributed to the koskari source location, not compiler internals
        self.assertFalse(cm.filename.endswith("compiler.py"))

    def test_paren_call_never_flagged(self):
        """( ) calls are never flagged, even under ERROR"""

        self.compile_src("(foo bar)", BracketCalls.ERROR)

    def test_let_bindings_are_immune(self):
        """[ ] binding groups are data to let, so never reach the check"""

        # (let [[a 1]] a) under ERROR must compile: the [[a 1]] bindings are
        # destructured as data and never compiled as a combination.
        self.compile_src("(let [[a 1]] a)", BracketCalls.ERROR)

    def test_bracket_wrapped_special_also_flagged(self):
        """the check sits above the special split, so [if ...] is flagged too"""

        with self.assertRaises(CompilerError):
            self.compile_src("[if x a b]", BracketCalls.ERROR)

    def test_string_flag_value_coerced(self):
        """a string flag value behaves like the matching enum member"""

        with self.assertRaises(CompilerError):
            self.compile_src("[foo bar]", "error")

    def test_property_coerces_and_identity_checks(self):
        """the bracket_calls property coerces str/member and rejects garbage"""

        module = Module(ModuleNamespace("test", {}))
        c = Compiler("<unittest>", module, bracket_calls="warn")
        self.assertIs(c.bracket_calls, BracketCalls.WARN)

        c.bracket_calls = "error"
        self.assertIs(c.bracket_calls, BracketCalls.ERROR)

        c.bracket_calls = BracketCalls.IGNORE
        self.assertIs(c.bracket_calls, BracketCalls.IGNORE)

        with self.assertRaises(ValueError):
            c.bracket_calls = "bogus"


# The end.
