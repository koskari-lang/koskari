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
tests.reproducers.test_mark_hygiene

Hygiene by marks: a name a template introduces is resolved by the
compiler under its marked spelling, so it binds and resolves apart
from a use-site name spelled the same, through nested macros, quote,
procedural arms, recursion, and macro-generating macros

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.module import load_module_source_async, new_module
from koskari.types import is_symbol, unpack


pytestmark = pytest.mark.phase_reproducers


async def load(name, src):
    module = new_module(name)
    vars(module)["__file__"] = f"<{name}>"
    await load_module_source_async(module, src)
    return vars(module)


class TestNestedMacroBinder(unittest.IsolatedAsyncioTestCase):
    """
    A binder a template writes through a nested macro stays the
    template's own: the use-site name it shadows is not captured
    """

    SRC = """
(define-syntax each-body
  (syntax-rules []
    [(each-body seq body) (list-each [x seq] body)]))

(define-syntax each-double
  (syntax-rules []
    [(each-double seq) (list-each [x seq] (* x 2))]))

(define x 100)
(define captured (each-body #[1 2 3] x))
(define doubled (each-double #[1 2 3]))
"""

    async def test_use_site_name_is_not_captured(self):
        ns = await load("mh_nested", self.SRC)
        self.assertEqual(list(ns["captured"]), [100, 100, 100])


    async def test_template_binder_still_binds_in_template(self):
        ns = await load("mh_nested_own", self.SRC)
        self.assertEqual(list(ns["doubled"]), [2, 4, 6])


class TestQuoteStripsMarks(unittest.IsolatedAsyncioTestCase):

    SRC = """
(define-syntax quoted-binder
  (syntax-rules []
    [(quoted-binder) (let [[t 1]] (cons t (quote (t)) nil))]))

(define result (quoted-binder))
"""

    async def test_quoted_symbol_is_bare(self):
        ns = await load("mh_quote", self.SRC)
        value, quoted = list(unpack(ns["result"]))
        self.assertEqual(value, 1)
        sym = list(unpack(quoted))[0]
        self.assertTrue(is_symbol(sym))
        self.assertEqual(str(sym), "t")


class TestSiblingDefinitions(unittest.IsolatedAsyncioTestCase):
    """
    An introduced closure may reference an introduced definition in
    either order: a function body completes when the fragment enclosing
    it does, when every sibling definition is declared
    """

    GOOD = """
(define-syntax mk-pair
  (syntax-rules []
    [(mk-pair)
     (begin
       (define helper (lambda [] 42))
       (define main (lambda [] (helper)))
       (main))]))

(define answer (mk-pair))
"""

    FORWARD = """
(define-syntax mk-pair
  (syntax-rules []
    [(mk-pair)
     (begin
       (define main (lambda [] (helper)))
       (define helper (lambda [] 42))
       (main))]))

(define answer (mk-pair))
"""

    async def test_definition_before_closure(self):
        ns = await load("mh_sibling_good", self.GOOD)
        self.assertEqual(ns["answer"], 42)


    async def test_definition_after_closure(self):
        ns = await load("mh_sibling_forward", self.FORWARD)
        self.assertEqual(ns["answer"], 42)


class TestMutualRecursionInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    labels and letrec expand to sibling definitions whose bodies name
    each other; a template that writes them must compile
    """

    SRC = """
(define-syntax parity
  (syntax-rules []
    [(parity n)
     (labels [[ev [k] (if (== k 0) True (od (- k 1)))]
              [od [k] (if (== k 0) False (ev (- k 1)))]]
       (ev n))]))

(define-syntax parity-rec
  (syntax-rules []
    [(parity-rec n)
     (letrec [[ev (lambda [k] (if (== k 0) True (od (- k 1))))]
              [od (lambda [k] (if (== k 0) False (ev (- k 1))))]]
       (ev n))]))

(define four (parity 4))
(define five (parity 5))
(define four-rec (parity-rec 4))
(define five-rec (parity-rec 5))
"""

    async def test_labels_in_template(self):
        ns = await load("mh_labels", self.SRC)
        self.assertTrue(ns["four"])
        self.assertFalse(ns["five"])


    async def test_letrec_in_template(self):
        ns = await load("mh_letrec", self.SRC)
        self.assertTrue(ns["four-rec"])
        self.assertFalse(ns["five-rec"])


class TestFreeReferenceBesideSiblingScope(unittest.IsolatedAsyncioTestCase):
    """
    A free template reference falls back to the use-site name even when
    a sibling scope in the same expansion bound the same spelling
    """

    SRC = """
(define tmp 100)

(define-syntax peek
  (syntax-rules []
    [(peek) (begin (let [[tmp 1]] tmp) tmp)]))

(define-syntax peek-lambda
  (syntax-rules []
    [(peek-lambda) (begin ((lambda [] (let [[tmp 1]] tmp))) ((lambda [] tmp)))]))

(define after-let (peek))
(define after-lambda (peek-lambda))
"""

    async def test_reference_after_sibling_let(self):
        ns = await load("mh_sibling_scope", self.SRC)
        self.assertEqual(ns["after-let"], 100)


    async def test_reference_in_sibling_lambda(self):
        ns = await load("mh_sibling_lambda", self.SRC)
        self.assertEqual(ns["after-lambda"], 100)


class TestMacroGeneratingMacro(unittest.IsolatedAsyncioTestCase):

    SRC = """
(define-syntax mk-getter
  (syntax-rules []
    [(mk-getter name val)
     (begin
       (define y val)
       (define-syntax name (syntax-rules [] [(name) y])))]))

(define y 100)
(mk-getter get-y 7)
(define got (get-y))
"""

    async def test_inner_template_reaches_outer_binder(self):
        ns = await load("mh_generating", self.SRC)
        self.assertEqual(ns["got"], 7)
        self.assertEqual(ns["y"], 100)


class TestProceduralArm(unittest.IsolatedAsyncioTestCase):

    SRC = """
(define-syntax with-tmp
  (macro-rules []
    [(with-tmp expr) `(let [[tmp 1]] (+ tmp ,expr))]))

(define tmp 100)
(define total (with-tmp tmp))
"""

    async def test_spliced_capture_keeps_use_site_binding(self):
        ns = await load("mh_procedural", self.SRC)
        self.assertEqual(ns["total"], 101)


class TestSetqFreeName(unittest.IsolatedAsyncioTestCase):

    SRC = """
(define-syntax bump
  (syntax-rules []
    [(bump) (setq counter (+ counter 1))]))

(define counter 0)
(bump)
(bump)
"""

    async def test_template_write_reaches_use_site_global(self):
        ns = await load("mh_setq", self.SRC)
        self.assertEqual(ns["counter"], 2)


class TestRecursiveMacro(unittest.IsolatedAsyncioTestCase):
    """
    A recursive template passes a use-site identifier into itself under
    its own binder of the same name
    """

    SRC = """
(define-syntax my-or
  (syntax-rules []
    [(my-or) False]
    [(my-or e) e]
    [(my-or e rest ...) (let [[t e]] (if t t (my-or rest ...)))]))

(define t 5)
(define picked (my-or False t))
(define first-wins (my-or 3 t))
"""

    async def test_use_site_name_survives_recursion(self):
        ns = await load("mh_recursive", self.SRC)
        self.assertEqual(ns["picked"], 5)
        self.assertEqual(ns["first-wins"], 3)


class TestTryInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    A try form a template writes carries a marked exception type; the
    try special rebuilds its clauses with pycons, which must accept a
    Mark as a leaf
    """

    SRC = """
(define-syntax guarded
  (syntax-rules []
    [(guarded body) (try body (except: ZeroDivisionError "caught"))]))

(define outcome (guarded (/ 1 0)))
(define passthru (guarded 7))
"""

    async def test_marked_exception_type_compiles(self):
        ns = await load("mh_try", self.SRC)
        self.assertEqual(ns["outcome"], "caught")
        self.assertEqual(ns["passthru"], 7)


class TestDefineGlobalInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    A global a template defines is bound under its marked spelling, and
    the template's own references reach it from the same scope, from a
    closure, and from an inline scope
    """

    SRC = """
(define-syntax top
  (syntax-rules []
    [(top) (begin (define-global ga 5) ga)]))

(define-syntax scoped
  (syntax-rules []
    [(scoped) (let [] (define-global gb 6) gb)]))

(define-syntax closed
  (syntax-rules []
    [(closed) (begin (define-global gc 7) ((lambda [] gc)))]))

(define-syntax scoped-closed
  (syntax-rules []
    [(scoped-closed) (let [] (define-global gd 8) ((lambda [] gd)))]))

(define a (top))
(define b (scoped))
(define c (closed))
(define d (scoped-closed))
"""

    async def test_global_visible_to_template_references(self):
        ns = await load("mh_define_global", self.SRC)
        self.assertEqual((ns["a"], ns["b"], ns["c"], ns["d"]), (5, 6, 7, 8))
        for bare in ("ga", "gb", "gc", "gd"):
            self.assertNotIn(bare, ns)


# The end.
