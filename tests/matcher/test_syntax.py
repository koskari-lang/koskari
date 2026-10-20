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
Tests for matcher self-describing matcher syntax: the META_MATCHER
walker (parse_matcher), the head registry (MATCHER_REGISTRY,
register_matcher, lookup_matcher), and end-to-end build/match of
patterns derived from surface s-expression syntax
"""


import unittest

import pytest

from koskari.lib import DatumFrame, cons, demand, keyword, nil, symbol
from koskari.matcher import (
    BINDING, BINDINGS, BODY, CAPTURE, CLASS, DECL, DEFINE, FIELD, FORM,
    FORMALS, GROUP, LIST, LITERAL, LITERAL_KW, LITERAL_SYM, MATCH, NOT, ONEOF,
    OPTIONAL, PAIR, PREDICATE, REPEAT, REST, SYMBOL, TEST_ANY, TYPE, USING,
    VALUE)
from koskari.matcher.heads import (
    MATCHER_REGISTRY, META_MATCHER, META_PATTERN, lookup_matcher,
    parse_matcher, register_matcher)


pytestmark = pytest.mark.phase_infra


def _list(*items):
    """
    Build a koskari proper list from items
    """

    if not items:
        return nil
    return cons(*items, nil)


class TestParseAtoms(unittest.TestCase):


    def test_integer_lowers_to_literal(self):
        """
        A bare integer element lowers to LITERAL holding the integer
        """

        pat = parse_matcher(5)
        self.assertIsInstance(pat, LITERAL)
        self.assertEqual(pat._value, 5)


    def test_string_lowers_to_literal(self):
        """
        A bare string element lowers to LITERAL holding the string
        """

        pat = parse_matcher("hello")
        self.assertIsInstance(pat, LITERAL)
        self.assertEqual(pat._value, "hello")


    def test_keyword_lowers_to_literal(self):
        """
        A bare keyword element lowers to LITERAL holding the keyword
        """

        kw = keyword("foo")
        pat = parse_matcher(kw)
        self.assertIsInstance(pat, LITERAL)
        self.assertIs(pat._value, kw)


    def test_true_false_none_lower_to_literal(self):
        """
        Booleans and None pass through the atom_literal arm
        """

        for value in (True, False, None):
            pat = parse_matcher(value)
            self.assertIsInstance(pat, LITERAL)
            self.assertIs(pat._value, value)


class TestParseBareSymbol(unittest.TestCase):


    def test_bare_symbol_lowers_to_capture(self):
        """
        A bare symbol element lowers to CAPTURE(name) with no
        sub-pattern
        """

        pat = parse_matcher(symbol("x"))
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "x")
        self.assertIsNone(pat._sub)


    def test_quoted_symbol_lowers_to_literal_sym(self):
        """
        (quote sym) lowers to LITERAL_SYM with the symbol name
        """

        src = _list(symbol("quote"), symbol("foo"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, LITERAL)
        self.assertIs(pat._value, symbol("foo"))


class TestParseAnonConstraints(unittest.TestCase):


    def test_dcolon_with_symbol_lowers_to_type(self):
        """
        (:: SYM) lowers to TYPE(SYM)
        """

        src = _list(keyword("::"), symbol("symbol"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, TYPE)
        self.assertEqual(pat._extern, "symbol")


    def test_dcolon_with_pair_lowers_via_recursion(self):
        """
        (:: (FORMALS f)) lowers via recursive parse_matcher of the
        payload
        """

        src = _list(keyword("::"),
                    _list(symbol("FORMALS"), symbol("f")))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, FORMALS)


    def test_type_kw_with_symbol_lowers_to_type(self):
        """
        (type: SYM) is a synonym for (:: SYM)
        """

        src = _list(keyword("type"), symbol("pair"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, TYPE)
        self.assertEqual(pat._extern, "pair")


    def test_val_kw_lowers_to_literal(self):
        """
        (val: expr) lowers to LITERAL holding the raw expr
        """

        src = _list(keyword("val"), 42)
        pat = parse_matcher(src)
        self.assertIsInstance(pat, LITERAL)
        self.assertEqual(pat._value, 42)


class TestParseNamedCapture(unittest.TestCase):


    def test_named_capture_with_dcolon_constraint(self):
        """
        (name :: symbol) lowers to CAPTURE("name", TYPE("symbol"))
        """

        src = _list(symbol("name"), keyword("::"), symbol("symbol"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "name")
        self.assertIsInstance(pat._sub, TYPE)
        self.assertEqual(pat._sub._extern, "symbol")


    def test_named_capture_with_type_kw(self):
        """
        (x type: bar) lowers to CAPTURE("x", TYPE("bar"))
        """

        src = _list(symbol("x"), keyword("type"), symbol("bar"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "x")
        self.assertIsInstance(pat._sub, TYPE)
        self.assertEqual(pat._sub._extern, "bar")


    def test_named_capture_with_val_kw(self):
        """
        (x val: 5) lowers to CAPTURE("x", LITERAL(5))
        """

        src = _list(symbol("x"), keyword("val"), 5)
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "x")
        self.assertIsInstance(pat._sub, LITERAL)
        self.assertEqual(pat._sub._value, 5)


    def test_named_capture_with_inline_subpattern(self):
        """
        (x (FORMALS f)) lowers with the inline form recursively
        parsed as the constraint
        """

        src = _list(symbol("x"),
                    _list(symbol("FORMALS"), symbol("f")))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "x")
        self.assertIsInstance(pat._sub, FORMALS)


    def test_named_capture_rejects_two_constraints(self):
        """
        Named capture accepts at most one constraint
        """

        src = _list(
            symbol("x"),
            keyword("::"), symbol("symbol"),
            keyword("type"), symbol("pair"))
        with self.assertRaises(ValueError):
            parse_matcher(src)


class TestParseCompositeVariadic(unittest.TestCase):


    def test_form_single_arg(self):
        """
        (FORM pat) lowers to FORM(parse_matcher(pat))
        """

        src = _list(symbol("FORM"), symbol("x"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, FORM)
        self.assertIsInstance(pat._sub, CAPTURE)


    def test_form_multi_arg_implicit_match(self):
        """
        (FORM pat1 pat2 ...) lowers with an implicit MATCH wrapper
        """

        src = _list(symbol("FORM"), symbol("a"), symbol("b"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, FORM)
        self.assertIsInstance(pat._sub, MATCH)
        self.assertEqual(len(pat._sub._children), 2)


    def test_match_lowers_children(self):
        """
        (MATCH a b c) lowers to MATCH(CAPTURE(a), CAPTURE(b), CAPTURE(c))
        """

        src = _list(symbol("MATCH"),
                    symbol("a"), symbol("b"), symbol("c"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, MATCH)
        self.assertEqual(len(pat._children), 3)


    def test_group_named_lowers_nested(self):
        """
        (GROUP slot pat...) lowers to GROUP(slot, ...)
        """

        src = _list(
            symbol("GROUP"), symbol("g"),
            symbol("a"), symbol("b"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, GROUP)
        self.assertEqual(pat._slot, "g")
        self.assertEqual(len(pat._children), 2)


    def test_group_nil_lowers_anonymous(self):
        """
        (GROUP nil pat...) lowers to GROUP(None, ...)
        """

        src = _list(symbol("GROUP"), nil, symbol("a"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, GROUP)
        self.assertIsNone(pat._slot)
        self.assertEqual(len(pat._children), 1)


    def test_oneof_lowers_branches(self):
        """
        (ONEOF a b) lowers to ONEOF with two branches
        """

        src = _list(symbol("ONEOF"), symbol("a"), symbol("b"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, ONEOF)
        self.assertEqual(len(pat._branches), 2)


    def test_not_unary(self):
        """
        (NOT pat) lowers to NOT(parse_matcher(pat))
        """

        src = _list(symbol("NOT"), symbol("a"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, NOT)
        self.assertIsInstance(pat._sub, CAPTURE)


    def test_repeat_unary(self):
        """
        (REPEAT pat) lowers to REPEAT with min_count 0 (Kleene star)
        """

        src = _list(symbol("REPEAT"), symbol("e"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, REPEAT)
        self.assertEqual(pat._min_count, 0)


    def test_repeat_slot_and_min_option(self):
        """
        (REPEAT slot pat min: N) lowers to repeat_capture with REPEAT
        min_count N
        """

        src = _list(
            symbol("REPEAT"), symbol("xs"), symbol("e"),
            keyword("min"), 2)
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "xs")
        self.assertIsInstance(pat._sub, REPEAT)
        self.assertEqual(pat._sub._min_count, 2)


    def test_repeat_min_keyword_with_trailing_colon(self):
        """
        min: spelling normalizes to the same option as min
        """

        src = _list(
            symbol("REPEAT"), symbol("ys"), symbol("f"),
            keyword("min:"), 0)
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._sub._min_count, 0)


    def test_optional_unary(self):
        """
        (OPTIONAL pat) lowers to OPTIONAL
        """

        src = _list(symbol("OPTIONAL"), symbol("e"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, OPTIONAL)


    def test_list_lowers_children(self):
        """
        (LIST a b) lowers to LIST(parse_matcher(a), parse_matcher(b))
        """

        src = _list(symbol("LIST"), symbol("a"), symbol("b"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, FORM)


class TestParseCompositeSlotFirst(unittest.TestCase):


    def test_binding(self):
        """
        (BINDING name) lowers to BINDING("name")
        """

        src = _list(symbol("BINDING"), symbol("name"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, BINDING)
        self.assertEqual(pat._slot, "name")


    def test_bindings(self):
        """
        (BINDINGS slot) lowers to BINDINGS("slot")
        """

        src = _list(symbol("BINDINGS"), symbol("slot"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, BINDINGS)
        self.assertEqual(pat._slot, "slot")


    def test_formals(self):
        """
        (FORMALS f) lowers to FORMALS("f")
        """

        src = _list(symbol("FORMALS"), symbol("f"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, FORMALS)
        self.assertEqual(pat._slot, "f")


    def test_using(self):
        """
        (USING u) lowers to USING("u")
        """

        src = _list(symbol("USING"), symbol("u"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, USING)
        self.assertEqual(pat._slot, "u")


    def test_define(self):
        """
        (DEFINE x) lowers to DEFINE("x")
        """

        src = _list(symbol("DEFINE"), symbol("x"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, DEFINE)
        self.assertEqual(pat._slot, "x")


    def test_decl(self):
        """
        (DECL d) lowers to DECL("d")
        """

        src = _list(symbol("DECL"), symbol("d"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, DECL)
        self.assertEqual(pat._slot, "d")


    def test_rest(self):
        """
        (REST tail) lowers to REST with min_tail 0
        """

        src = _list(symbol("REST"), symbol("tail"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, REST)
        self.assertEqual(pat._slot, "tail")
        self.assertEqual(pat._min_tail, 0)


    def test_rest_min_option(self):
        """
        (REST slot min: N) sets min_tail
        """

        src = _list(
            symbol("REST"), symbol("tail"),
            keyword("min"), 1)
        pat = parse_matcher(src)
        self.assertIsInstance(pat, REST)
        self.assertEqual(pat._min_tail, 1)


    def test_rest_nil_lowers_bare_rest(self):
        """
        (REST) lowers to REST with no capture slot
        """

        src = _list(symbol("REST"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, REST)
        self.assertIsNone(pat._slot)
        self.assertEqual(pat._min_tail, 0)


    def test_tail(self):
        """
        (TAIL tail) lowers to OPTIONAL(REST("tail", min_tail=1))
        """

        src = _list(symbol("TAIL"), symbol("tail"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, OPTIONAL)
        self.assertIsInstance(pat._sub, REST)
        self.assertEqual(pat._sub._slot, "tail")
        self.assertEqual(pat._sub._min_tail, 1)


    def test_body(self):
        """
        (BODY body) lowers to BODY("body") with scope-region meta
        """

        src = _list(symbol("BODY"), symbol("body"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, BODY)
        self.assertEqual(pat._slot, "body")


    def test_value(self):
        """
        (VALUE expr) lowers to VALUE("expr") with scope-region meta
        """

        src = _list(symbol("VALUE"), symbol("expr"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, VALUE)
        self.assertEqual(pat._slot, "expr")


    def test_symbol_helper(self):
        """
        (SYMBOL slot) lowers to SYMBOL("slot") -> CAPTURE+TYPE
        """

        src = _list(symbol("SYMBOL"), symbol("name"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "name")
        self.assertIsInstance(pat._sub, TYPE)
        self.assertEqual(pat._sub._extern, "symbol")


    def test_pair_helper(self):
        """
        (PAIR slot) lowers to PAIR("slot") -> CAPTURE+TYPE("pair")
        """

        src = _list(symbol("PAIR"), symbol("p"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "p")
        self.assertIsInstance(pat._sub, TYPE)
        self.assertEqual(pat._sub._extern, "pair")


class TestParseAtomFactories(unittest.TestCase):


    def test_literal_takes_raw_value(self):
        """
        (LITERAL value) does not recurse into value via parse_matcher
        """

        src = _list(symbol("LITERAL"), symbol("foo"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, LITERAL)
        self.assertIs(pat._value, symbol("foo"))


    def test_type_factory(self):
        """
        (TYPE name) lowers to TYPE("name")
        """

        src = _list(symbol("TYPE"), symbol("symbol"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, TYPE)
        self.assertEqual(pat._extern, "symbol")


    def test_predicate_factory(self):
        """
        (PREDICATE name) lowers to PREDICATE("name")
        """

        src = _list(symbol("PREDICATE"), symbol("is_even"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, PREDICATE)
        self.assertEqual(pat._extern, "is_even")


    def test_question_mark_is_predicate(self):
        """
        (? name) is the alias of (PREDICATE name)
        """

        pat = parse_matcher(_list(symbol("?"), symbol("is_even")))
        self.assertIsInstance(pat, PREDICATE)
        self.assertEqual(pat._extern, "is_even")


    def test_literal_kw_factory(self):
        """
        (LITERAL_KW name) lowers to LITERAL_KW("name")
        """

        src = _list(symbol("LITERAL_KW"), symbol("as"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, LITERAL)
        self.assertIs(pat._value, keyword("as"))


    def test_literal_sym_factory(self):
        """
        (LITERAL_SYM name) lowers to LITERAL_SYM("name")
        """

        src = _list(symbol("LITERAL_SYM"), symbol("when"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, LITERAL)
        self.assertIs(pat._value, symbol("when"))


    def test_test_any_factory(self):
        """
        (TEST_ANY) lowers to TEST_ANY()
        """

        src = _list(symbol("TEST_ANY"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, TEST_ANY)


class TestParseClass(unittest.TestCase):


    def test_class_with_positional_fields(self):
        """
        (CLASS Foo (FIELD x) (FIELD y)) lowers to CLASS with two
        positional FIELD children
        """

        src = _list(
            symbol("CLASS"), symbol("Foo"),
            _list(symbol("FIELD"), symbol("x")),
            _list(symbol("FIELD"), symbol("y")))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CLASS)
        self.assertEqual(pat._extern, "Foo")
        self.assertEqual(len(pat._fields), 2)
        for f in pat._fields:
            self.assertIsInstance(f, FIELD)
            self.assertIsNone(f._name)


    def test_class_with_named_field(self):
        """
        (CLASS Foo (FIELD x pat)) binds the field name to "x"
        """

        src = _list(
            symbol("CLASS"), symbol("Foo"),
            _list(symbol("FIELD"), symbol("x"), symbol("pat")))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CLASS)
        self.assertEqual(len(pat._fields), 1)
        self.assertEqual(pat._fields[0]._name, "x")


    def test_class_no_fields(self):
        """
        (CLASS Foo) is valid and lowers to CLASS("Foo") with empty
        fields tuple
        """

        src = _list(symbol("CLASS"), symbol("Foo"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CLASS)
        self.assertEqual(pat._extern, "Foo")
        self.assertEqual(pat._fields, ())


class TestRecursiveLowering(unittest.TestCase):


    def test_form_with_named_capture_and_bare(self):
        """
        Walker round-trips (FORM (name :: symbol) expr) into a
        FORM(MATCH(CAPTURE(name, TYPE(symbol)), CAPTURE(expr)))
        and the resulting matcher matches the worked-example shape
        """

        src = _list(
            symbol("FORM"),
            _list(symbol("name"), keyword("::"), symbol("symbol")),
            symbol("expr"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, FORM)
        self.assertIsInstance(pat._sub, MATCH)
        children = pat._sub._children
        self.assertEqual(len(children), 2)
        self.assertIsInstance(children[0], CAPTURE)
        self.assertEqual(children[0]._slot, "name")
        self.assertIsInstance(children[0]._sub, TYPE)
        self.assertIsInstance(children[1], CAPTURE)
        self.assertEqual(children[1]._slot, "expr")

        m = MATCH(pat).build()
        sa = symbol("foo")
        sv = symbol("bar")
        subj = (_list(sa, sv),)
        result = demand(m)(subj)
        self.assertIsNotNone(result)
        self.assertIs(result["name"].datum, sa)
        self.assertIs(result["expr"].datum, sv)


    def test_oneof_with_alternative_arms(self):
        """
        (ONEOF (a :: symbol) (b :: pair)) builds and matches either
        arm
        """

        src = _list(
            symbol("ONEOF"),
            _list(symbol("a"), keyword("::"), symbol("symbol")),
            _list(symbol("b"), keyword("::"), symbol("pair")))
        pat = parse_matcher(src)
        m = MATCH(pat).build()

        sa = symbol("hello")
        result = demand(m)((sa,))
        self.assertIsNotNone(result)
        self.assertIs(result["a"], sa)
        self.assertNotIn("b", result)


    def test_binding_inside_form(self):
        """
        (FORM (BINDING b) expr) parses and matches a binding-shape
        embedded in a larger form
        """

        src = _list(
            symbol("FORM"),
            _list(symbol("BINDING"), symbol("b")),
            symbol("expr"))
        pat = parse_matcher(src)
        m = MATCH(pat).build()

        sname = symbol("x")
        sval = symbol("v")
        cell = _list(sname, sval)
        outer = _list(cell, sval)
        result = demand(m)((outer,))
        self.assertIsNotNone(result)
        b = result["b"]
        self.assertIsInstance(b, DatumFrame)
        self.assertIs(b.source, cell)
        self.assertIs(result["b"]["name"].datum, sname)
        self.assertIs(result["b"]["expr"].datum, sval)


class TestRegistry(unittest.TestCase):


    def test_lookup_known_heads(self):
        """
        Built-in heads resolve via lookup_matcher
        """

        for name in ("FORM", "MATCH", "GROUP", "ONEOF", "BINDING", "FORMALS",
                     "CAPTURE", "REST", "TAIL", "TYPE"):
            self.assertIsNotNone(lookup_matcher(name))


    def test_lookup_unknown_returns_none(self):
        """
        Unregistered head returns None
        """

        self.assertIsNone(lookup_matcher("ZZZ_NOT_A_HEAD"))


    def test_register_matcher_user_factory(self):
        """
        A user-registered head dispatches via parse_matcher
        """

        def my_factory(head_sym, raw_args):
            return CAPTURE("user_thing")

        register_matcher("MYTHING_FOR_TEST", my_factory)
        try:
            src = _list(symbol("MYTHING_FOR_TEST"))
            pat = parse_matcher(src)
            self.assertIsInstance(pat, CAPTURE)
            self.assertEqual(pat._slot, "user_thing")
        finally:
            del MATCHER_REGISTRY["MYTHING_FOR_TEST"]


    def test_register_matcher_idempotent(self):
        """
        Re-registering the same name with the same callable is a
        no-op; registering a different callable raises ValueError
        """

        def f1(head_sym, raw_args):
            return TEST_ANY()

        def f2(head_sym, raw_args):
            return TEST_ANY()

        register_matcher("IDEMPOTENT_FOR_TEST", f1)
        try:
            register_matcher("IDEMPOTENT_FOR_TEST", f1)
            with self.assertRaises(ValueError):
                register_matcher("IDEMPOTENT_FOR_TEST", f2)
        finally:
            del MATCHER_REGISTRY["IDEMPOTENT_FOR_TEST"]


    def test_register_matcher_validates_arguments(self):
        """
        register_matcher rejects non-string names, empty names, and
        non-callable factories
        """

        with self.assertRaises(TypeError):
            register_matcher(42, lambda *a: None)
        with self.assertRaises(ValueError):
            register_matcher("", lambda *a: None)
        with self.assertRaises(TypeError):
            register_matcher("X", "not a callable")


class TestUnknownHeadFallthrough(unittest.TestCase):


    def test_unknown_head_becomes_named_capture(self):
        """
        (formals :: (FORMALS f)) — head 'formals' is not registered,
        so it falls through to lower_named_capture
        """

        src = _list(
            symbol("formals"),
            keyword("::"),
            _list(symbol("FORMALS"), symbol("f")))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "formals")
        self.assertIsInstance(pat._sub, FORMALS)


    def test_unknown_head_no_constraints(self):
        """
        (somename) without args lowers to CAPTURE("somename") with
        no sub
        """

        src = _list(symbol("somename"))
        pat = parse_matcher(src)
        self.assertIsInstance(pat, CAPTURE)
        self.assertEqual(pat._slot, "somename")
        self.assertIsNone(pat._sub)


class TestParseErrors(unittest.TestCase):


    def test_empty_list_rejected(self):
        """
        nil is not a valid matcher pattern element
        """

        with self.assertRaises(ValueError):
            parse_matcher(nil)


    def test_dotted_pair_rejected(self):
        """
        An improper-list head element (cons of two atoms) does not
        satisfy any meta-pattern arm
        """

        improper = cons(symbol("FORM"), symbol("trailing"))
        with self.assertRaises(ValueError):
            parse_matcher(improper)


    def test_unknown_keyword_in_named_capture(self):
        """
        Unrecognised keyword inside a named-capture body raises
        """

        src = _list(symbol("x"), keyword("zzz"), 1)
        with self.assertRaises(ValueError):
            parse_matcher(src)


    def test_anon_constraint_missing_payload(self):
        """
        (::) with no payload raises
        """

        src = _list(keyword("::"))
        with self.assertRaises(ValueError):
            parse_matcher(src)


    def test_anon_constraint_extra_payload(self):
        """
        (:: a b) is rejected as ambiguous
        """

        src = _list(keyword("::"), symbol("a"), symbol("b"))
        with self.assertRaises(ValueError):
            parse_matcher(src)


    def test_form_zero_args_rejected(self):
        """
        (FORM) with no sub-pattern raises ValueError
        """

        src = _list(symbol("FORM"))
        with self.assertRaises(ValueError):
            parse_matcher(src)


    def test_oneof_zero_args_rejected(self):
        """
        (ONEOF) with no branches raises ValueError
        """

        src = _list(symbol("ONEOF"))
        with self.assertRaises(ValueError):
            parse_matcher(src)


    def test_slot_only_factory_arity(self):
        """
        (BINDING) with no slot raises; (BINDING a b) with extra args
        raises
        """

        with self.assertRaises(ValueError):
            parse_matcher(_list(symbol("BINDING")))
        with self.assertRaises(ValueError):
            parse_matcher(_list(symbol("BINDING"), symbol("a"), symbol("b")))


    def test_slot_first_slot_must_be_symbol(self):
        """
        (BINDING 5) with a non-symbol slot name raises ValueError
        """

        src = _list(symbol("BINDING"), 5)
        with self.assertRaises(ValueError):
            parse_matcher(src)


    def test_class_field_must_lower_to_field(self):
        """
        (CLASS Foo bare_sym) where bare_sym does not lower to FIELD
        raises ValueError
        """

        src = _list(symbol("CLASS"), symbol("Foo"), symbol("bare"))
        with self.assertRaises(ValueError):
            parse_matcher(src)


    def test_field_arity_rejected(self):
        """
        (FIELD) with no args raises; three or more args raise
        """

        src_zero = _list(symbol("FIELD"))
        src_three = _list(
            symbol("FIELD"), symbol("a"), symbol("b"), symbol("c"))
        with self.assertRaises(ValueError):
            parse_matcher(src_zero)
        with self.assertRaises(ValueError):
            parse_matcher(src_three)


    def test_bindings_unknown_option(self):
        """
        (BINDINGS slot foo: bar) raises on unknown option
        """

        src = _list(symbol("BINDINGS"), symbol("s"),
                    keyword("foo"), symbol("bar"))
        with self.assertRaises(ValueError):
            parse_matcher(src)


class TestMetaMatcherExposed(unittest.TestCase):


    def test_meta_pattern_is_the_oneof_builder(self):
        """
        META_PATTERN, the pattern META_MATCHER is built from, is the
        underlying ONEOF builder
        """

        self.assertIsInstance(META_PATTERN, ONEOF)


    def test_meta_matcher_directly_against_quote(self):
        """
        Calling META_MATCHER directly on a (quote x) form binds the
        quoted_symbol slot
        """

        src = _list(symbol("quote"), symbol("x"))
        result = META_MATCHER((src,))
        self.assertIsNotNone(result)
        self.assertIs(
            result["quoted_symbol"].datum,
            symbol("x"))

# The end.
