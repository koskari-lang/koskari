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
tests.reproducers.test_template_datumq

The datumq template directive: (datumq X) in a syntax-rules template
emits X raw, unmarked, so templates can write a name they share with
the use site; nested directives peel one level per template

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.exceptions import CompilerError
from koskari.module import load_module_source_async, new_module


pytestmark = pytest.mark.phase_reproducers


async def load(name, src):
    module = new_module(name)
    vars(module)["__file__"] = f"<{name}>"
    await load_module_source_async(module, src)
    return vars(module)


class TestSharedName(unittest.IsolatedAsyncioTestCase):
    """
    Two templates share a module-level name through the directive, and
    the use site sees it under its written name
    """

    SRC = """
(define-syntax set-slot
  (syntax-rules [] [(set-slot v) (define (datumq shared-slot) v)]))
(define-syntax read-slot
  (syntax-rules [] [(read-slot) (datumq shared-slot)]))

(set-slot 7)
(define via-macro (read-slot))
(define via-name shared-slot)
"""

    async def test_templates_share_the_raw_name(self):
        ns = await load("tdq_shared", self.SRC)
        self.assertEqual(ns["via-macro"], 7)
        self.assertEqual(ns["via-name"], 7)


class TestRawForm(unittest.IsolatedAsyncioTestCase):
    """
    A pattern variable inside the directive is not substituted, and the
    directive's form does not drive an ellipsis
    """

    SRC = """
(define-syntax raw-name
  (syntax-rules [] [(raw-name x) (quote (datumq x))]))
(define-syntax tagged
  (syntax-rules []
    [(tagged x ...) (#list (#tuple x (quote (datumq x))) ...)]))

(define raw (raw-name 5))
(define rows (tagged 1 2))
"""

    async def test_pattern_variable_stays_raw(self):
        ns = await load("tdq_raw", self.SRC)
        self.assertEqual(str(ns["raw"]), "x")


    async def test_directive_repeats_under_ellipsis(self):
        ns = await load("tdq_ellipsis", self.SRC)
        self.assertEqual(
            [(n, str(t)) for n, t in ns["rows"]], [(1, "x"), (2, "x")])


class TestNestedDirective(unittest.IsolatedAsyncioTestCase):
    """
    A macro-generating template nests the directive: the outer
    expansion emits the inner directive raw, and the inner expansion
    writes the raw name
    """

    SRC = """
(define-syntax make-setter
  (syntax-rules []
    [(make-setter mname)
     (define-syntax mname
       (syntax-rules [] [(mname v) (define (datumq (datumq slot)) v)]))]))

(make-setter set-slot)
(set-slot 11)
(define got slot)
"""

    async def test_inner_template_writes_the_raw_name(self):
        ns = await load("tdq_nested", self.SRC)
        self.assertEqual(ns["got"], 11)


class TestDatumIsOrdinary(unittest.IsolatedAsyncioTestCase):
    """
    datum in a template is an ordinary call: a template that defines a
    procedural macro writes it with no escape
    """

    SRC = """
(define-syntax make-suffixer
  (syntax-rules []
    [(make-suffixer mname)
     (define-syntax mname
       (macro-rules [] [(mname s) (datum (symbol f"{s.datum}-x"))]))]))

(make-suffixer suffixed)
(define abc-x 5)
(define got (suffixed abc))
"""

    async def test_datum_call_passes_through(self):
        ns = await load("tdq_datum", self.SRC)
        self.assertEqual(ns["got"], 5)


class TestArity(unittest.IsolatedAsyncioTestCase):
    """
    The directive takes exactly one form
    """

    SRC = """
(define-syntax bad (syntax-rules [] [(bad) (datumq a b)]))
(bad)
"""

    async def test_two_forms_are_refused(self):
        with self.assertRaises(CompilerError) as caught:
            await load("tdq_arity", self.SRC)
        self.assertIn("exactly one form", str(caught.exception))


# The end.
