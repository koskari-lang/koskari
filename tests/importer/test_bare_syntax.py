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
Tests for bare syntax forms: define-syntax and extend-syntax with a
bare-syntax kind, lone-atom expansion, bare-only alias behavior in form
position, and the bootstrap bare control words.

Requires import koskari.builtins so bootstrap definitions are loaded.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest
import unittest

from koskari.compile import Compiler
from koskari.exceptions import SyntaxMatchError
from koskari.lib import Module, ModuleNamespace
from koskari.reader import default_reader, source_str


pytestmark = pytest.mark.phase_basics


class BareSyntaxHelpers(unittest.IsolatedAsyncioTestCase):
    """
    Compile and evaluate forms sequentially in one shared module, so a
    syntax defined by one form is bound before a later form expands it
    """

    def make_module(self):
        import koskari.builtins as builtins
        mod_ns = ModuleNamespace("test", vars(builtins))
        module = Module(mod_ns)
        compiler = Compiler("<unittest>", module, filename=__file__)
        return module, mod_ns, compiler


    async def run_forms(self, *srcs):
        """
        Read, compile, and evaluate each source string in order in a
        shared module namespace; return the last form's value
        """

        module, mod_ns, compiler = self.make_module()
        result = None
        for lineno, src in enumerate(srcs, 1):
            source = default_reader.read(
                source_str(src, __file__, line_no=lineno))
            fragment = compiler.full_compile(source)
            expr = fragment.as_expression(mod_ns)
            result = await expr()
        return result


BARE_ALIAS = """
(define-syntax adding (bare-syntax me +))
"""

BARE_QUOTED = """
(define-syntax greeting (bare-syntax me (quote hello)))
"""


class TestBareAtom(BareSyntaxHelpers):


    async def test_bare_atom_expands(self):
        result = await self.run_forms(
            BARE_QUOTED,
            "greeting")
        self.assertEqual(str(result), "hello")


    async def test_extend_syntax_attaches_bare(self):
        result = await self.run_forms(
            "(define-syntax pick (syntax-rules [] [(pick x) x]))",
            "(extend-syntax pick (bare-syntax me (quote alone)))",
            "pick")
        self.assertEqual(str(result), "alone")


class TestBareOnlyAlias(BareSyntaxHelpers):
    """
    A bare-only syntax used in form position is an alias: the head
    expands and the tail rides along
    """


    async def test_alias_in_form_position(self):
        result = await self.run_forms(
            BARE_ALIAS,
            "(adding 1 2 3)")
        self.assertEqual(result, 6)


    async def test_alias_in_nested_head_position(self):
        result = await self.run_forms(
            "(define mk (lambda mk [a] (lambda inner [b c] (+ a b c))))",
            "(define-syntax mk! (bare-syntax me mk))",
            "((mk! 1) 2 3)")
        self.assertEqual(result, 6)


    async def test_alias_expanding_to_attr_form(self):
        result = await self.run_forms(
            '(define-syntax dash (bare-syntax me (attr "-" join)))',
            '(dash #["a" "b"])')
        self.assertEqual(result, "a-b")


class TestMixedBareAndCall(BareSyntaxHelpers):
    """
    A syntax with call arms never falls back to its bare arm: a form no
    call arm matches is a malformed use and raises
    """


    async def test_call_arm_and_bare_arm_both_work(self):
        result = await self.run_forms(
            "(define-syntax pick (syntax-rules [] [(pick x) x]))",
            "(extend-syntax pick (bare-syntax me (quote alone)))",
            "(pick 42)")
        self.assertEqual(result, 42)


    async def test_no_matching_call_arm_raises(self):
        with self.assertRaises(SyntaxMatchError):
            await self.run_forms(
                "(define-syntax pick (syntax-rules [] [(pick x) x]))",
                "(extend-syntax pick (bare-syntax me (quote alone)))",
                "(pick 1 2)")


class TestBareControlWords(BareSyntaxHelpers):
    """
    Bootstrap bare forms for the statement words: the lone symbol
    expands to the call form
    """


    async def test_bare_return(self):
        result = await self.run_forms(
            "(define f (lambda f [x] (begin (when x return) 99)))",
            "#[(f True) (f False)]")
        self.assertEqual(result, [None, 99])


    async def test_bare_continue(self):
        result = await self.run_forms(
            "(define out #[])",
            "(for-each [x #[1 2 3]]"
            "  (when (== x 2) continue)"
            "  (.append out x))",
            "out")
        self.assertEqual(result, [1, 3])


    async def test_bare_break(self):
        result = await self.run_forms(
            "(define out #[])",
            "(for-each [x #[1 2 3]]"
            "  (when (== x 2) break)"
            "  (.append out x))",
            "out")
        self.assertEqual(result, [1])


    async def test_bare_yield(self):
        result = await self.run_forms(
            "(define gen (lambda gen [] (begin yield (yield 2))))",
            "(list (gen))")
        self.assertEqual(result, [None, 2])


    async def test_bare_raise_reraises(self):
        result = await self.run_forms(
            '(try'
            '  (try (raise (ValueError "boom"))'
            '    ([e ValueError] raise))'
            '  ([e2 ValueError] "caught"))')
        self.assertEqual(result, "caught")


# The end.
