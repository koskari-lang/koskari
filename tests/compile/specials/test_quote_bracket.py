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
quote reconstructs bracket-pair structure, not just plain pairs.

A quoted list that is not const-pooled is rebuilt at runtime; requote now
rebuilds a bracket spine via a runtime bracket-cons call, so [ ] survives
the round-trip.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.bootstrap import make_bootstrap
from koskari.compile import Compiler
from koskari.lib import Module, ModuleNamespace, bracket_pair, kos_repr, pair
from koskari.reader import Reader, source_str


pytestmark = pytest.mark.phase_specials


class _BracketEvalCase(unittest.IsolatedAsyncioTestCase):
    """Shared read -> compile -> eval helper over the full builtins."""

    async def eval_src(self, src):
        # full builtins so the runtime bracket-cons/bracket-coerce resolve
        mod_ns = ModuleNamespace("test", dict(make_bootstrap()))
        module = Module(mod_ns)
        compiler = Compiler("<unittest>", module, filename=__file__)
        source = Reader().read(source_str(src, "<unittest>"))
        fragment = compiler.full_compile(source)
        expr = fragment.as_expression(mod_ns)
        return await expr()


class TestQuoteBracket(_BracketEvalCase):
    """'[...] evaluates back to bracket-pair structure"""

    async def test_paren_list_unchanged(self):
        """'(foo bar) still reconstructs as a paren list"""

        self.assertEqual(kos_repr(await self.eval_src("'(foo bar)")),
                         "(foo bar)")

    async def test_paren_with_bracket_inner(self):
        """'(foo [bar]) keeps the inner bracket sublist"""

        self.assertEqual(kos_repr(await self.eval_src("'(foo [bar])")),
                         "(foo [bar])")

    async def test_bracket_list(self):
        """'[foo [bar]] reconstructs as nested bracket lists"""

        self.assertEqual(kos_repr(await self.eval_src("'[foo [bar]]")),
                         "[foo [bar]]")

    async def test_bracket_with_paren_inner(self):
        """'[foo (bar)] keeps the bracket outer and paren inner"""

        self.assertEqual(kos_repr(await self.eval_src("'[foo (bar)]")),
                         "[foo (bar)]")

    async def test_empty_bracket(self):
        """'[] evaluates to the empty bracket list"""

        self.assertEqual(kos_repr(await self.eval_src("'[]")), "[]")

    async def test_quoted_bracket_equals_paren(self):
        """the reconstructed bracket list equals the paren one structurally"""

        b = await self.eval_src("'[1 2 3]")
        p = await self.eval_src("'(1 2 3)")
        self.assertEqual(b, p)


class TestQuasiquoteBracket(_BracketEvalCase):
    """`[...] reconstructs bracket structure, with unquote/splice"""

    async def test_static_bracket(self):
        """a static `[a b] reconstructs as a bracket list"""

        self.assertEqual(kos_repr(await self.eval_src("`[a b]")), "[a b]")

    async def test_static_paren_unchanged(self):
        """a static `(a b) still reconstructs as a paren list"""

        self.assertEqual(kos_repr(await self.eval_src("`(a b)")), "(a b)")

    async def test_unquote_in_bracket(self):
        """`[a ,(+ 1 2) b] fills the unquote and keeps brackets"""

        self.assertEqual(kos_repr(await self.eval_src("`[a ,(+ 1 2) b]")),
                         "[a 3 b]")

    async def test_nested_bracket_in_paren(self):
        """a bracket sublist inside a paren quasiquote keeps its brackets"""

        self.assertEqual(
            kos_repr(await self.eval_src("`(a [b ,(+ 1 2)])")),
            "(a [b 3])")

    async def test_splice_in_bracket(self):
        """,@ splices into a bracket list, result stays bracketed"""

        self.assertEqual(
            kos_repr(await self.eval_src("`[a ,@'(1 2) b]")),
            "[a 1 2 b]")

    async def test_dotted_bracket(self):
        """an improper bracket quasiquote keeps the dotted tail"""

        self.assertEqual(kos_repr(await self.eval_src("`[a ,(+ 1 2) . b]")),
                         "[a 3 . b]")

    async def test_splice_head_is_bracket_only(self):
        """a spliced bracket list has a bracket head and a plain-pair tail"""

        # bracket-splice marks only the leading cell (repr by leading cell);
        # the rest of the spliced spine stays plain pairs.
        r = await self.eval_src("`[a ,@'(1 2) b]")
        self.assertIsInstance(r, bracket_pair)
        self.assertIs(type(r.cdr), pair)

    async def test_static_bracket_is_fully_bracketed(self):
        """a non-spliced bracket list builds bracket cells throughout"""

        r = await self.eval_src("`[a b]")
        self.assertIs(type(r), bracket_pair)
        self.assertIs(type(r.cdr), bracket_pair)


# The end.
