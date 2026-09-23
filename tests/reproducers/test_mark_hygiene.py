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

from koskari.lib import keyword, symbol
from koskari.module import load_module_source_async, new_module
from koskari.types import strip_mark, unpack


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
        self.assertIs(type(sym), symbol)
        self.assertIs(sym, symbol("t"))


class TestQuasiquoteStripsMarks(unittest.IsolatedAsyncioTestCase):
    """
    A symbol a template writes inside a quasiquote is data: the runtime
    value is the plain interned symbol by exact type, since a gensym
    would print the same spelling
    """

    SRC = """
(define-syntax tagged
  (syntax-rules []
    [(tagged v) `(tag ,v)]))

(define result (tagged 5))
"""

    async def test_quasiquoted_symbol_is_bare(self):
        ns = await load("mh_quasiquote", self.SRC)
        head, value = list(unpack(ns["result"]))
        self.assertIs(type(head), symbol)
        self.assertIs(head, symbol("tag"))
        self.assertEqual(value, 5)


class TestByRefKeywordFormalInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    A by-ref keyword formal a template writes binds under the name the
    caller writes, so the caller can pass a cell for it by that name
    """

    SRC = """
(define-syntax mk-fill
  (syntax-rules []
    [(mk-fill name) (define name [&out: 99] out)]))

(mk-fill fill)
(define mine)
(fill out: &mine)
(define filled mine)
"""

    async def test_caller_name_is_bare(self):
        ns = await load("mh_byref_keyword", self.SRC)
        self.assertEqual(ns["filled"], 99)
        self.assertEqual(ns["fill"].formals.kwd, ("out",))
        self.assertEqual(ns["fill"].formals.by_refs, ("out",))


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


class TestNestedClosureOverModuleBinder(unittest.IsolatedAsyncioTestCase):
    """
    A template's module-level definition is visible from a function
    body at any nesting depth, in the same compilation unit as the
    definition
    """

    DEPTH_TWO = """
(define-syntax d2
  (syntax-rules []
    [(d2 getter)
     (begin
       (define secret 11)
       (define getter [] (lambda [] secret)))]))

(d2 gg)
(define answer ((gg)))
"""

    MIXED = """
(define-syntax d2
  (syntax-rules []
    [(d2 getter)
     (begin
       (define secret 11)
       (define getter [] (+ secret ((lambda [] secret)))))]))

(d2 gg)
(define answer (gg))
"""

    async def test_depth_two_reference(self):
        ns = await load("mh_depth_two", self.DEPTH_TWO)
        self.assertEqual(ns["answer"], 11)


    async def test_depth_one_and_two_in_one_expansion(self):
        ns = await load("mh_depth_mixed", self.MIXED)
        self.assertEqual(ns["answer"], 22)


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


class TestProceduralInnerMacro(unittest.IsolatedAsyncioTestCase):
    """
    A procedural macro a template defines builds its output with
    quasiquote-syntax, which keeps the outer template's marks on the
    symbols it wrote, so the output reaches the outer expansion's
    binder. A plain quasiquote is data and projects the marks, so the
    same output resolves at the use site
    """

    SRC = """
(define-syntax mk-getter
  (syntax-rules []
    [(mk-getter name val)
     (begin
       (define secret val)
       (define-syntax name (macro-rules [] [(name) #`secret])))]))

(define secret 100)
(mk-getter get-secret 8)
(define got (get-secret))
"""

    PLAIN = """
(define-syntax mk-getter
  (syntax-rules []
    [(mk-getter name val)
     (begin
       (define secret val)
       (define-syntax name (macro-rules [] [(name) `secret])))]))

(define secret 100)
(mk-getter get-secret 8)
(define got (get-secret))
"""

    async def test_inner_procedural_reaches_outer_binder(self):
        ns = await load("mh_generating_procedural", self.SRC)
        self.assertEqual(ns["got"], 8)
        self.assertEqual(ns["secret"], 100)


    async def test_plain_quasiquote_is_data(self):
        ns = await load("mh_generating_procedural_plain", self.PLAIN)
        self.assertEqual(ns["got"], 100)


class TestScopedCaptureUnderQuote(unittest.IsolatedAsyncioTestCase):
    """
    A scoped symbol an outer template hands to an inner macro, which
    quotes it as data, arrives at runtime as the plain symbol
    """

    SRC = """
(define-syntax as-data
  (syntax-rules []
    [(as-data x) (quote (x))]))

(define-syntax outer
  (syntax-rules []
    [(outer) (let [[tmp 1]] (as-data tmp))]))

(define result (outer))
"""

    async def test_captured_scoped_symbol_is_plain_under_quote(self):
        ns = await load("mh_capture_quoted", self.SRC)
        sym = list(unpack(ns["result"]))[0]
        self.assertIs(type(sym), symbol)
        self.assertIs(sym, symbol("tmp"))


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
    A try form a template writes carries a scoped exception type; the
    try special rebuilds its clauses with pycons, which must accept a
    gensym as a leaf
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


class TestGeneratedMatchPatterns(unittest.IsolatedAsyncioTestCase):
    """
    A syntax-match or macro-match transformer a template defines
    carries marked pattern atoms; the registry head and slot must still
    lower, and the slot must keep the mark the template references
    """

    SRC = """
(define-syntax mk-double
  (syntax-rules []
    [(mk-double name)
     (define-syntax name (syntax-match [(name (VALUE x)) (+ x x)]))]))

(define-syntax mk-triple
  (syntax-rules []
    [(mk-triple name)
     (define-syntax name (macro-match [(name (VALUE x)) `(* 3 ,x)]))]))

(mk-double twice)
(mk-triple thrice)
(define doubled (twice 21))
(define tripled (thrice 5))
"""

    async def test_generated_syntax_match(self):
        ns = await load("mh_gen_syntax_match", self.SRC)
        self.assertEqual(ns["doubled"], 42)


    async def test_generated_macro_match(self):
        ns = await load("mh_gen_macro_match", self.SRC)
        self.assertEqual(ns["tripled"], 15)


class TestPatternTagsInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    The keyword tags and payloads of a pattern a template writes are
    marked; a :: annotation, a quoted symbol, and a min: option are read
    by their bare spelling
    """

    SRC = """
(define-syntax classify
  (syntax-rules []
    [(classify v)
     (match v
       [[x :: int] when: (> x 10) "big"]
       [[x :: int] "small"]
       [['go] "go"]
       [else: "other"])]))

(define-syntax at-least-one
  (syntax-rules []
    [(at-least-one name)
     (define-syntax name (syntax-match [(name (REST r min: 1)) (quote r)]))]))

(at-least-one some)
(define big (classify 42))
(define small (classify 3))
(define go (classify 'go))
(define other (classify "x"))
(define gathered (some 1 2))
"""

    async def test_annotated_case_pattern(self):
        ns = await load("mh_pattern_tags", self.SRC)
        self.assertEqual(
            (ns["big"], ns["small"], ns["go"], ns["other"]),
            ("big", "small", "go", "other"))


    async def test_generated_min_option(self):
        ns = await load("mh_pattern_min", self.SRC)
        self.assertEqual(list(unpack(ns["gathered"])), [1, 2])


class TestClassPatternInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    A class pattern a template writes names the class and its keyword
    fields by their bare spelling, and binds its captures under the
    marked one
    """

    SRC = """
(define-class Point []
  (define __match_args__ #("x" "y"))
  (define __init__ [self x y]
    (set-attr self x x)
    (set-attr self y y)))

(define-syntax sum-point
  (syntax-rules []
    [(sum-point v)
     (match v
       [[Point[x y]] (+ x y)]
       [else: "no"])]))

(define-syntax y-of
  (syntax-rules []
    [(y-of v)
     (match v
       [[Point[y: y]] y]
       [else: "no"])]))

(define-syntax self-y
  (syntax-rules []
    [(self-y v)
     (match v
       [[Point[x: 1 y]] y]
       [else: "no"])]))

(define summed (sum-point (Point 1 2)))
(define missed (sum-point 3))
(define keyed (y-of (Point 1 2)))
(define selfed (self-y (Point 1 5)))
"""

    async def test_positional_fields(self):
        ns = await load("mh_class_pattern", self.SRC)
        self.assertEqual(ns["summed"], 3)
        self.assertEqual(ns["missed"], "no")


    async def test_keyword_and_self_binding_fields(self):
        ns = await load("mh_class_pattern_kw", self.SRC)
        self.assertEqual(ns["keyed"], 2)
        self.assertEqual(ns["selfed"], 5)


class TestGeneratedKeywordLiteral(unittest.IsolatedAsyncioTestCase):
    """
    A keyword literal in a rule pattern a template writes matches a
    use-site keyword by its bare spelling
    """

    SRC = """
(define-syntax mk-pick
  (syntax-rules []
    [(mk-pick name)
     (define-syntax name (syntax-rules [] [(name a then: b) b]))]))

(mk-pick pick)
(define picked (pick 1 then: 2))
"""

    async def test_keyword_literal_matches(self):
        ns = await load("mh_keyword_literal", self.SRC)
        self.assertEqual(ns["picked"], 2)


class TestQuasiquotedBlockPeers(unittest.IsolatedAsyncioTestCase):
    """
    A block quasiquote in a template is lowered by the layout handlers
    after transcription marked its heads; a peer block such as else
    must still be gathered under its bare head
    """

    SRC = """
define-syntax guard-form:
  syntax-rules []:
    guard-form test a b:
      `begin:
        if test:
          a
        else:
          b

define form (guard-form True 1 2)
"""

    async def test_else_peer_is_gathered(self):
        ns = await load("mh_block_peers", self.SRC)
        outer = list(unpack(ns["form"]))
        self.assertEqual(len(outer), 2)
        cond = list(unpack(outer[1]))
        self.assertEqual(len(cond), 3)
        last = list(unpack(cond[2]))
        self.assertIs(strip_mark(last[0]), keyword("else"))
        self.assertEqual(last[1], 2)


class TestImportInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    An import a template writes binds under the template's marked
    spelling: the template's own references reach it and a use-site
    binding of the same name is untouched
    """

    SRC = """
(define environ "mine")
(define os "mine too")
(define p "and me")

(define-syntax os-environ
  (syntax-rules []
    [(os-environ) (begin (import-from os environ) environ)]))

(define-syntax os-sep
  (syntax-rules []
    [(os-sep) (begin (import os) os.sep)]))

(define-syntax path-sep
  (syntax-rules []
    [(path-sep) (begin (import [os.path as: p]) p.sep)]))

(define imported-environ (os-environ))
(define imported-sep (os-sep))
(define renamed-sep (path-sep))
"""

    async def test_import_from_member(self):
        ns = await load("mh_import_from", self.SRC)
        self.assertNotEqual(ns["imported-environ"], "mine")
        self.assertEqual(ns["environ"], "mine")


    async def test_import_module_and_renamed_member(self):
        ns = await load("mh_import", self.SRC)
        self.assertIsInstance(ns["imported-sep"], str)
        self.assertEqual(ns["imported-sep"], ns["renamed-sep"])
        self.assertEqual(ns["os"], "mine too")
        self.assertEqual(ns["p"], "and me")


class TestClassMembersInTemplate(unittest.IsolatedAsyncioTestCase):
    """
    A member a template defines in a class body is an attribute of the
    class, named by its bare spelling
    """

    SRC = """
(define-syntax mk-greeter
  (syntax-rules []
    [(mk-greeter name)
     (define-class name []
       (define greeting "hi")
       (define greet [self] (#str self.greeting " there")))]))

(mk-greeter Greeter)
(define greeter (Greeter))
(define said (greeter.greet))
"""

    async def test_member_names_are_bare(self):
        ns = await load("mh_class_members", self.SRC)
        self.assertEqual(ns["said"], "hi there")
        names = [n for n in dir(ns["Greeter"]) if not n.startswith("_")]
        self.assertEqual(sorted(names), ["greet", "greeting"])


# The end.
