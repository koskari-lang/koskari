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
The stray_hash compiler flag: allow / warn / error on a bare ``#``
symbol compiled as a name.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest
import warnings

import pytest

from koskari.bootstrap import make_bootstrap
from koskari.compile import Compiler
from koskari.compile.pragma import StrayColon, StrayHash
from koskari.exceptions import CompilerError
from koskari.lib import Module, ModuleNamespace
from koskari.reader import Reader, source_str


pytestmark = pytest.mark.phase_compile


class TestStrayHashLint(unittest.TestCase):
    """stray_hash gates compiling a bare # symbol as a name"""

    def compile_src(self, src, stray_hash, stray_colon=StrayColon.ALLOW):
        mod_ns = ModuleNamespace("test", dict(make_bootstrap()))
        module = Module(mod_ns)
        compiler = Compiler("<unittest>", module, filename=__file__,
                            stray_colon=stray_colon,
                            stray_hash=stray_hash)
        source = Reader().read(source_str(src, "<unittest>"))
        return compiler.full_compile(source)

    def test_allow_compiles(self):
        """ALLOW: a stray # compiles as an ordinary name lookup"""

        self.compile_src("(foo bar # baz)", StrayHash.ALLOW)

    def test_error_raises(self):
        """ERROR: a stray # raises a CompilerError"""

        with self.assertRaises(CompilerError):
            self.compile_src("(foo bar # baz)", StrayHash.ERROR)

    def test_warn_warns_but_compiles(self):
        """WARN: a stray # warns, attributed to the source, not compiler.py"""

        with self.assertWarns(SyntaxWarning) as cm:
            self.compile_src("(foo bar # baz)", StrayHash.WARN)
        self.assertFalse(cm.filename.endswith("compiler.py"))

    def test_warn_names_the_enclosing_form_line(self):
        """WARN: the hash is interned, so it borrows the form's line"""

        src = "(begin\n  (foo # [1 2]))"
        with self.assertWarns(SyntaxWarning) as cm:
            self.compile_src(src, StrayHash.WARN)
        self.assertEqual(2, cm.lineno)

    def test_warn_reports_each_hash_once(self):
        """WARN: one diagnostic per stray hash, head position included"""

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            self.compile_src("(# foo)", StrayHash.WARN)
        self.assertEqual(1, len(caught))

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            self.compile_src("(foo # bar #)", StrayHash.WARN)
        self.assertEqual(2, len(caught))

    def test_hash_in_head_position_flagged(self):
        """a # reaching the callable slot is diagnosed like any other"""

        with self.assertRaises(CompilerError):
            self.compile_src("(# foo)", StrayHash.ERROR)

    def test_collection_never_flagged(self):
        """#[ #( #{ are collection openers, not a stray hash"""

        self.compile_src("(foo #[1 2] #(3 4) #{a: 5})", StrayHash.ERROR)

    def test_hash_prefixed_name_never_flagged(self):
        """#tuple is a name of its own, not a stray hash"""

        self.compile_src("(#tuple 1 2)", StrayHash.ERROR)

    def test_quoted_hash_is_immune(self):
        """a quoted # is data, never compiled as a name"""

        self.compile_src("'(foo # bar)", StrayHash.ERROR)

    def test_independent_of_stray_colon(self):
        """stray_colon does not govern a #, nor stray_hash a :"""

        self.compile_src("(foo # bar)", StrayHash.ALLOW, StrayColon.ERROR)
        self.compile_src("(foo : bar)", StrayHash.ERROR, StrayColon.ALLOW)

    def test_string_flag_value_coerced(self):
        """a string flag value behaves like the matching enum member"""

        with self.assertRaises(CompilerError):
            self.compile_src("(foo bar # baz)", "error")

    def test_property_coerces_and_identity_checks(self):
        """the stray_hash property coerces str/member and rejects garbage"""

        module = Module(ModuleNamespace("test", {}))
        c = Compiler("<unittest>", module, stray_hash="warn")
        self.assertIs(c.stray_hash, StrayHash.WARN)

        c.stray_hash = "error"
        self.assertIs(c.stray_hash, StrayHash.ERROR)

        c.stray_hash = StrayHash.ALLOW
        self.assertIs(c.stray_hash, StrayHash.ALLOW)

        with self.assertRaises(ValueError):
            c.stray_hash = "bogus"

    def test_pragma_baseline_reaches_compiler(self):
        """an undeclared compiler/stray-hash loads the WARN baseline"""

        module = Module(ModuleNamespace("test", {}))
        c = Compiler.from_pragma("<unittest>", module)
        self.assertIs(c.stray_hash, StrayHash.WARN)


# The end.
