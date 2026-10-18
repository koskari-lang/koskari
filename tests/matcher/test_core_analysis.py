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
tests.matcher.test_core_analysis

The matcher core's analysis of a pattern's tables: the refusals at
build, the shapes it accepts, the facts it hands the emitter, and its
refusals over the oracle corpus against master's outcomes

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import ModuleNamespace, demand, nil, pair, symbol
from koskari.match_code import compile_match_function
from koskari.match_core import FIELD_LIMIT, Emitter, analyse
from koskari.matcher import (
    APP, CAPTURE, CLASS, FIELD, FORM, GROUP, LITERAL, MAP, MAP_ENTRY, MATCH,
    NOT, ONEOF, OPTIONAL, PREDICATE, REPEAT, REST, SAME, TEST_ANY, TYPE,
    MatchCompiler, parse_case_pattern, repeat_capture)
from koskari.reader import Reader, source_str

from tests.matcher.oracle import RULES, build_pattern, load_corpus


pytestmark = pytest.mark.phase_infra


# the oracle rules that explain a refusal at build
REFUSAL_RULES = (
    'pair_literal_refused_inline',
    'non_pool_literal_refused',
    'refused_single_value_width',
    'refused_field_width',
    'refused_repeat_zero_body',
    'refused_variable_after_rest',
    'refused_capture_under_not',
)


class TestRefusals(unittest.TestCase):

    def test_non_pool_literal(self):
        tables = MatchCompiler().lower(MATCH(LITERAL([1, 2])))
        with self.assertRaisesRegex(TypeError, "literal at node 0"):
            analyse(tables)


    def test_non_pool_literal_in_a_nested_value(self):
        tables = MatchCompiler().lower(
            MATCH(APP("f", MATCH(LITERAL(object())))))
        with self.assertRaisesRegex(TypeError, "not a constant: object"):
            analyse(tables)


    def test_non_pool_mapping_key(self):
        tables = MatchCompiler().lower(MATCH(MAP(MAP_ENTRY(object()))))
        with self.assertRaisesRegex(TypeError, "mapping key at node 0"):
            analyse(tables)


    def test_repeat_body_may_consume_nothing(self):
        for body in (OPTIONAL(TEST_ANY()), NOT(LITERAL(1)), MATCH(),
                     REST()):
            tables = MatchCompiler().lower(MATCH(REPEAT(body)))
            with self.assertRaisesRegex(ValueError, "REPEAT at node"):
                analyse(tables)


    def test_repeat_count_past_the_field_limit(self):
        tables = MatchCompiler().lower(
            MATCH(REPEAT(TEST_ANY(), min_count=FIELD_LIMIT + 1)))
        with self.assertRaisesRegex(ValueError, "min_count"):
            analyse(tables)


    def test_rest_count_past_the_field_limit(self):
        tables = MatchCompiler().lower(MATCH(REST(min_tail=FIELD_LIMIT + 1)))
        with self.assertRaisesRegex(ValueError, "min_tail"):
            analyse(tables)


    def test_length_bounds_past_the_field_limit(self):
        tables = MatchCompiler().lower(
            MATCH(REPEAT(MATCH(TEST_ANY(), TEST_ANY()), min_count=0x8000)))
        with self.assertRaisesRegex(ValueError, "length bounds"):
            analyse(tables)


    def test_field_not_one_wide(self):
        for sub in (OPTIONAL(TEST_ANY()), MATCH(TEST_ANY(), TEST_ANY()),
                    NOT(LITERAL(1)), REST()):
            tables = MatchCompiler().lower(MATCH(CLASS("Pt", FIELD(sub))))
            with self.assertRaisesRegex(ValueError, "FIELD at node"):
                analyse(tables)


    def test_app_value_width_without_one(self):
        for sub in (NOT(LITERAL(1)), MATCH(TEST_ANY(), TEST_ANY())):
            tables = MatchCompiler().lower(MATCH(APP("f", sub)))
            with self.assertRaisesRegex(ValueError, "APP at node"):
                analyse(tables)


    def test_map_value_width_without_one(self):
        for entry in (MAP_ENTRY("k", NOT(LITERAL(1))),
                      MAP_ENTRY("K", MATCH(TEST_ANY(), TEST_ANY()),
                                extern=True)):
            tables = MatchCompiler().lower(MATCH(MAP(entry)))
            with self.assertRaisesRegex(ValueError, "MAP_ENTRY_"):
                analyse(tables)


    def test_arm_root_width_without_one(self):
        for root in (NOT(LITERAL(1)), MATCH(TEST_ANY(), TEST_ANY()),
                     MATCH()):
            tables = MatchCompiler().lower(MATCH(root))
            with self.assertRaisesRegex(ValueError, "match arm root"):
                analyse(tables, arm=True)


    def test_same_in_an_arm(self):
        tables = MatchCompiler().lower(MATCH(
            FORM(MATCH(CAPTURE("x"), SAME("x")))))
        analyse(tables)
        with self.assertRaisesRegex(ValueError, "SAME at node 1"):
            analyse(tables, arm=True)


    def test_positional_field_after_keyword(self):
        tables = MatchCompiler().lower(MATCH(CLASS(
            "Pt", FIELD(TEST_ANY(), name="y"), FIELD(TEST_ANY()))))
        with self.assertRaisesRegex(ValueError, "positional FIELD"):
            analyse(tables)


    def test_capture_in_a_not_subtree(self):
        for sub in (CAPTURE("x"), GROUP("g", TEST_ANY()), REST("r"),
                    FORM(MATCH(CAPTURE("x"))),
                    APP("f", CAPTURE("x"))):
            tables = MatchCompiler().lower(MATCH(NOT(sub), TEST_ANY()))
            with self.assertRaisesRegex(ValueError, "in a NOT subtree"):
                analyse(tables)


    def test_variable_width_after_rest(self):
        for tail in (OPTIONAL(TEST_ANY()), REST(), REPEAT(TEST_ANY()),
                     ONEOF(TEST_ANY(), MATCH(TEST_ANY(), TEST_ANY()))):
            tables = MatchCompiler().lower(MATCH(REST(), tail))
            with self.assertRaisesRegex(ValueError, "REST at node"):
                analyse(tables)


    def test_variable_width_after_a_rest_in_a_branch(self):
        tables = MatchCompiler().lower(MATCH(
            ONEOF(REST(), TEST_ANY()), OPTIONAL(TEST_ANY())))
        with self.assertRaisesRegex(ValueError, "REST at node"):
            analyse(tables)


    def test_same_at_depth_zero_in_a_repeat_naming_an_outer_slot(self):
        # MATCH(CAPTURE("x"), REPEAT(GROUP(None, SAME("x")))): a pass
        # over a GROUP body is a depth of its own, so depth 0 in it
        # does not reach the root's slot, and the SAME always misses
        slots = ('x',)
        nodes = (
            ('CAPTURE', 0, -1),
            ('SAME', 0, 0),
            ('GROUP', 1),
            ('REPEAT', 2, 0),
            ('MATCH', 0, 3),
        )
        self.assertEqual(analyse((slots, (), (), nodes, 4)).same, {1: None})

        # any other pass shares the depth of the scope around it
        tables = MatchCompiler().lower(
            MATCH(CAPTURE("x"), REPEAT(SAME("x"))))
        found = analyse(tables)
        same = [i for i, n in enumerate(tables[3]) if n[0] == 'SAME'][0]
        self.assertEqual(found.same[same], (0, 0))


    def test_same_past_the_root(self):
        # MATCH(CAPTURE("x"), SAME("x", depth=1)): the depth passes the
        # root, so the SAME always misses, as on master
        slots = ('x',)
        nodes = (
            ('CAPTURE', 0, -1),
            ('SAME', 0, 1),
            ('MATCH', 0, 1),
        )
        tables = (slots, (), (), nodes, 2)
        self.assertEqual(analyse(tables).same, {1: None})

        matcher = demand(
            compile_match_function(
                tables, analyse(tables), ModuleNamespace("probe", {})))
        self.assertIsNone(matcher((1, 1)))
        self.assertIsNone(matcher((1, 2)))


    def test_same_at_depth_zero_in_an_anonymous_group(self):
        # MATCH(CAPTURE("x"), GROUP(None, SAME("x"))): the anonymous
        # GROUP's scope is depth 0 and does not bind x, so the SAME
        # always misses
        slots = ('x',)
        nodes = (
            ('CAPTURE', 0, -1),
            ('SAME', 0, 0),
            ('GROUP', 1),
            ('MATCH', 0, 2),
        )
        self.assertEqual(analyse((slots, (), (), nodes, 3)).same, {1: None})


    def test_same_naming_a_closed_group_slot(self):
        # MATCH(GROUP("g", CAPTURE("x")), SAME("x")) with the SAME
        # resolved by name, as MatchCompiler does not: the root binds
        # no x, so the SAME always misses
        slots = ('g', 'x')
        nodes = (
            ('CAPTURE', 1, -1),
            ('GROUP', 0),
            ('CAPTURE', 0, 1),
            ('SAME', 1, 0),
            ('MATCH', 2, 3),
        )
        self.assertEqual(analyse((slots, (), (), nodes, 4)).same, {3: None})


    def test_branch_name_reused_after_its_oneof(self):
        tables = MatchCompiler().lower(MATCH(
            ONEOF(CAPTURE("x"), TEST_ANY()), CAPTURE("x")))
        with self.assertRaisesRegex(ValueError, "'x' is bound twice"):
            analyse(tables)


    def test_repeat_column_equal_to_an_outer_name(self):
        tables = MatchCompiler().lower(MATCH(
            CAPTURE("x"), REPEAT(GROUP(None, CAPTURE("x")))))
        with self.assertRaisesRegex(ValueError, "'x' is bound twice"):
            analyse(tables)


    def test_repeat_column_under_a_capture_of_its_name(self):
        tables = MatchCompiler().lower(MATCH(
            CAPTURE("x", MATCH(REPEAT(GROUP(None, CAPTURE("x")))))))
        with self.assertRaisesRegex(ValueError, "'x' is bound twice"):
            analyse(tables)


    def test_name_twice_in_one_alternative(self):
        tables = MatchCompiler().lower(MATCH(ONEOF(
            MATCH(CAPTURE("x"), REPEAT(GROUP(None, CAPTURE("x")))),
            TEST_ANY())))
        with self.assertRaisesRegex(ValueError, "'x' is bound twice"):
            analyse(tables)


class TestAccepted(unittest.TestCase):

    def test_capture_over_a_possibly_empty_sub_pattern(self):
        for sub in (OPTIONAL(TEST_ANY()), MATCH(), NOT(LITERAL(1)),
                    MATCH(OPTIONAL(TEST_ANY()), REPEAT(TEST_ANY()))):
            analyse(MatchCompiler().lower(MATCH(CAPTURE("x", sub))))


    def test_repeat_followed_by_rest(self):
        tables = MatchCompiler().lower(MATCH(
            REPEAT(LITERAL(1)), REST("r"), LITERAL(2)))
        found = analyse(tables)
        nodes = tables[3]
        repeat = nodes.index(('REPEAT', 0, 0))
        rest = nodes.index(('REST', 0))
        self.assertEqual(found.suffix[repeat], (1, None))
        self.assertEqual(found.suffix[rest], (1, 1))


    def test_same_after_a_oneof(self):
        # the tables MatchCompiler gives once a branch's names stay
        # visible after the ONEOF: per-branch slot indices of one name
        slots = ('a', 'a')
        nodes = (
            ('CAPTURE', 0, -1),
            ('TEST', 'LITERAL', 0),
            ('MATCH', 0, 1),
            ('CAPTURE', 1, -1),
            ('ONEOF', 2, 3),
            ('SAME', 1, 0),
            ('MATCH', 4, 5),
        )
        found = analyse((slots, (), (1,), nodes, 6))
        self.assertEqual(found.scopes[0].names, ('a',))
        self.assertEqual(found.slot_index, (0, 0))
        self.assertEqual(found.same, {5: (0, 0)})


    def test_same_before_its_slot_is_bound(self):
        slots = ('a',)
        nodes = (
            ('SAME', 0, 0),
            ('CAPTURE', 0, -1),
            ('MATCH', 0, 1),
        )
        found = analyse((slots, (), (), nodes, 2))
        self.assertEqual(found.same, {0: (0, 0)})


    def test_arm_roots_of_each_kind(self):
        roots = (
            TEST_ANY(),
            LITERAL(1),
            TYPE("int"),
            TYPE("Pt"),
            PREDICATE("p"),
            CAPTURE("x"),
            CAPTURE("x", TYPE("int")),
            FORM(MATCH(CAPTURE("x"), REST("r"))),
            APP("f", CAPTURE("x")),
            CLASS("Pt", FIELD(CAPTURE("x"))),
            MAP(MAP_ENTRY("k", CAPTURE("x"))),
            MATCH(NOT(LITERAL(1)), TEST_ANY()),
            ONEOF(LITERAL(1), CAPTURE("x")),
            OPTIONAL(CAPTURE("x")),
            REPEAT(CAPTURE("x")),
            REPEAT(TEST_ANY(), min_count=1),
            REST("r"),
            REST(min_tail=1),
            GROUP(None, CAPTURE("x")),
            GROUP("g", CAPTURE("x")),
            repeat_capture("xs", CAPTURE("x")),
        )
        for root in roots:
            tables = MatchCompiler().lower(MATCH(root))
            found = analyse(tables, arm=True)
            self.assertIsNone(found.root_fits)


    def test_name_across_the_alternatives_of_one_oneof(self):
        tables = MatchCompiler().lower(MATCH(ONEOF(
            CAPTURE("x"),
            ONEOF(MATCH(CAPTURE("x"), CAPTURE("y")),
                  REPEAT(CAPTURE("x"), min_count=1)),
            GROUP(None, CAPTURE("x")))))
        found = analyse(tables)
        self.assertEqual(found.scopes[0].names, ('x', 'y'))


    def test_same_name_in_separate_scopes(self):
        analyse(MatchCompiler().lower(MATCH(
            CAPTURE("x"), GROUP("g", CAPTURE("x")),
            GROUP(None, CAPTURE("x")), repeat_capture("xs", CAPTURE("x")))))


    def test_group_in_a_value(self):
        for value in (GROUP(None, TEST_ANY()), GROUP("g", CAPTURE("x"))):
            for pattern in (APP("f", value), MAP(MAP_ENTRY("k", value)),
                            CLASS("Pt", FIELD(value))):
                analyse(MatchCompiler().lower(MATCH(pattern)))


class TestFacts(unittest.TestCase):

    def test_widths(self):
        tables = MatchCompiler().lower(MATCH(
            TEST_ANY(), OPTIONAL(TEST_ANY()), NOT(LITERAL(1)),
            REPEAT(MATCH(TEST_ANY(), TEST_ANY()), min_count=2),
            CLASS("Pt", FIELD(TEST_ANY()))))
        found = analyse(tables)
        nodes = tables[3]
        self.assertEqual(found.widths[tables[4]], (6, None))
        self.assertFalse(found.fixed[tables[4]])
        field = [i for i, n in enumerate(nodes) if n[0] == 'FIELD_POS'][0]
        self.assertIsNone(found.widths[field])
        self.assertIsNone(found.suffix[field])
        optional = [i for i, n in enumerate(nodes) if n[0] == 'OPTIONAL'][0]
        self.assertEqual(found.widths[optional], (0, 1))


    def test_suffix_ends_at_the_cursor_owner(self):
        tables = MatchCompiler().lower(MATCH(
            FORM(MATCH(REPEAT(TEST_ANY()), LITERAL(1))),
            REPEAT(LITERAL(2)), OPTIONAL(LITERAL(3)), LITERAL(4)))
        found = analyse(tables)
        nodes = tables[3]
        inner, outer = [i for i, n in enumerate(nodes) if n[0] == 'REPEAT']
        self.assertEqual(found.suffix[inner], (1, 1))
        self.assertEqual(found.suffix[outer], (1, 2))


    def test_suffix_of_a_repeat_body_is_the_pass(self):
        tables = MatchCompiler().lower(MATCH(
            REPEAT(MATCH(REPEAT(LITERAL(1)), LITERAL(2))), LITERAL(3)))
        found = analyse(tables)
        nodes = tables[3]
        inner = nodes.index(('REPEAT', 0, 0))
        self.assertEqual(found.suffix[inner], (1, 1))


    def test_fits(self):
        tables = MatchCompiler().lower(MATCH(
            TEST_ANY(),
            FORM(MATCH(TEST_ANY(), TEST_ANY())),
            ONEOF(MATCH(LITERAL(1), LITERAL(2)), LITERAL(3)),
            REPEAT(GROUP(None, TEST_ANY(), TEST_ANY()))))
        found = analyse(tables)
        nodes = tables[3]
        entry = tables[4]
        form = [i for i, n in enumerate(nodes) if n[0] == 'FORM'][0]
        oneof = [i for i, n in enumerate(nodes) if n[0] == 'ONEOF'][0]
        group = [i for i, n in enumerate(nodes) if n[0] == 'GROUP'][0]
        branch = nodes[oneof][1]
        self.assertEqual(found.root_fits, (3, None))
        self.assertEqual(found.fits, {
            branch: (2, None),
            group: (2, None),
        })
        self.assertEqual(found.form_fits, {form: (2, 2)})
        # the root MATCH and the MATCH under the FORM repeat their
        # owner's bounds at the same position
        self.assertNotIn(entry, found.fits)
        self.assertNotIn(nodes[form][1], found.fits)


    def test_fits_of_a_one_wide_alternative(self):
        # a ONEOF branch or OPTIONAL sub-pattern of minimum width 1
        # checks the suffix's minimum after its element, with no max;
        # with nothing after it on its cursor it checks nothing
        tables = MatchCompiler().lower(MATCH(
            OPTIONAL(CAPTURE("a")),
            ONEOF(CAPTURE("b"), MATCH()),
            CAPTURE("c"),
            OPTIONAL(CAPTURE("d"))))
        found = analyse(tables)
        nodes = tables[3]
        first, last = [n[1] for n in nodes if n[0] == 'OPTIONAL']
        oneof = [i for i, n in enumerate(nodes) if n[0] == 'ONEOF'][0]
        branch = nodes[oneof][1]
        self.assertEqual(found.fits, {
            first: (2, None),
            branch: (2, None),
        })
        self.assertNotIn(last, found.fits)


    def test_a_check_implied_by_an_earlier_one_is_omitted(self):
        # a FORM checks its exact length (2, 2); the ONEOF branch at the
        # same position needs only (2, None), which that check implies
        tables = MatchCompiler().lower(MATCH(FORM(MATCH(ONEOF(
            MATCH(TEST_ANY(), TEST_ANY()),
            MATCH(LITERAL(1), TEST_ANY()))))))
        found = analyse(tables)
        nodes = tables[3]
        form = [i for i, n in enumerate(nodes) if n[0] == 'FORM'][0]
        oneof = [i for i, n in enumerate(nodes) if n[0] == 'ONEOF'][0]
        self.assertEqual(found.form_fits, {form: (2, 2)})
        for branch in nodes[oneof][1:]:
            self.assertNotIn(branch, found.fits)


    def test_named_group_scope(self):
        tables = MatchCompiler().lower(MATCH(
            CAPTURE("a"), GROUP("g", CAPTURE("b"), SAME("a", depth=1))))
        found = analyse(tables)
        nodes = tables[3]
        group = [i for i, n in enumerate(nodes) if n[0] == 'GROUP'][0]
        scope = found.group_scope[group]
        self.assertEqual(found.scopes[0].names, ('a', 'g'))
        self.assertEqual(found.scopes[scope].names, ('b',))
        self.assertEqual(found.scopes[scope].kind, 'group')
        self.assertEqual(found.scopes[scope].parent, 0)
        same = [i for i, n in enumerate(nodes) if n[0] == 'SAME'][0]
        self.assertEqual(found.same[same], (0, 0))


    def test_anonymous_group_scope(self):
        tables = MatchCompiler().lower(MATCH(
            CAPTURE("a"),
            OPTIONAL(GROUP(None, CAPTURE("b"), SAME("a", depth=1)))))
        found = analyse(tables)
        nodes = tables[3]
        group = [i for i, n in enumerate(nodes) if n[0] == 'GROUP'][0]
        optional = [i for i, n in enumerate(nodes) if n[0] == 'OPTIONAL'][0]
        same = [i for i, n in enumerate(nodes) if n[0] == 'SAME'][0]
        scope = found.group_scope[group]
        self.assertEqual(found.scopes[0].names, ('a',))
        self.assertEqual(found.scopes[scope].kind, 'discard')
        self.assertEqual(found.scopes[scope].names, ('b',))
        self.assertEqual(found.scopes[scope].parent, 0)
        self.assertEqual(found.node_scope[group], scope)
        self.assertEqual(found.same[same], (0, 0))
        # the discarded captures are not the OPTIONAL's to clear
        self.assertEqual(found.branch_slots[group], ())
        self.assertEqual(nodes[optional][1], group)


    def test_anonymous_group_root(self):
        tables = MatchCompiler().lower(GROUP(None, CAPTURE("a")))
        found = analyse(tables)
        self.assertEqual(found.scopes[0].names, ())
        self.assertEqual(found.scopes[1].kind, 'discard')
        self.assertEqual(found.scopes[1].names, ('a',))


    def test_repeat_body_group_is_the_pass(self):
        tables = MatchCompiler().lower(MATCH(
            repeat_capture("rows", CAPTURE("r"))))
        found = analyse(tables)
        nodes = tables[3]
        group = [i for i, n in enumerate(nodes) if n[0] == 'GROUP'][0]
        repeat = [i for i, n in enumerate(nodes) if n[0] == 'REPEAT'][0]
        self.assertNotIn(group, found.group_scope)
        self.assertEqual(found.node_scope[group], found.pass_scope[repeat])
        self.assertEqual(len(found.scopes), 2)


    def test_oneof_branches_share_an_index(self):
        tables = MatchCompiler().lower(MATCH(ONEOF(
            CAPTURE("x"), MATCH(CAPTURE("y"), CAPTURE("x")))))
        found = analyse(tables)
        self.assertEqual(tables[0], ('x', 'y'))
        self.assertEqual(found.scopes[0].names, ('x', 'y'))
        self.assertEqual(found.slot_index, (0, 1))
        oneof = tables[3][tables[3][tables[4]][1]]
        self.assertEqual(found.branch_slots[oneof[1]], (0,))
        self.assertEqual(found.branch_slots[oneof[2]], (0, 1))


    def test_optional_slots(self):
        tables = MatchCompiler().lower(MATCH(
            CAPTURE("a"),
            OPTIONAL(MATCH(CAPTURE("b"), GROUP("g", CAPTURE("c"))))))
        found = analyse(tables)
        nodes = tables[3]
        optional = [i for i, n in enumerate(nodes) if n[0] == 'OPTIONAL'][0]
        self.assertEqual(found.scopes[0].names, ('a', 'b', 'g'))
        self.assertEqual(found.branch_slots[nodes[optional][1]], (1, 2))


    def test_repeat_columns(self):
        tables = MatchCompiler().lower(MATCH(
            CAPTURE("a"),
            REPEAT(MATCH(CAPTURE("k"), REPEAT(CAPTURE("v"), min_count=1))),
            repeat_capture("rows", CAPTURE("r"))))
        found = analyse(tables)
        nodes = tables[3]
        repeats = [i for i, n in enumerate(nodes) if n[0] == 'REPEAT']
        inner, outer, captured = repeats
        self.assertEqual(found.scopes[0].names, ('a', 'k', 'v', 'rows'))
        outer_pass = found.scopes[found.pass_scope[outer]]
        self.assertEqual(outer_pass.names, ('k', 'v'))
        self.assertEqual(outer_pass.kind, 'pass')
        self.assertEqual(found.columns, {inner: (1,), outer: (1, 2)})
        self.assertNotIn(captured, found.columns)
        self.assertEqual(found.scopes[found.pass_scope[captured]].names,
                         ('r',))


    def test_oneof_slots_include_repeat_columns(self):
        tables = MatchCompiler().lower(MATCH(ONEOF(
            REPEAT(CAPTURE("x"), min_count=1), CAPTURE("y"))))
        found = analyse(tables)
        oneof = tables[3][tables[3][tables[4]][1]]
        self.assertEqual(found.branch_slots[oneof[1]], (0,))
        self.assertEqual(found.branch_slots[oneof[2]], (1,))


    def test_same_in_a_repeat_pass(self):
        tables = MatchCompiler().lower(MATCH(
            REPEAT(MATCH(CAPTURE("x"), SAME("x")))))
        found = analyse(tables)
        nodes = tables[3]
        repeat = [i for i, n in enumerate(nodes) if n[0] == 'REPEAT'][0]
        same = [i for i, n in enumerate(nodes) if n[0] == 'SAME'][0]
        self.assertEqual(found.same[same], (found.pass_scope[repeat], 0))


    def test_same_after_a_repeat_reads_its_column(self):
        tables = MatchCompiler().lower(MATCH(
            REPEAT(CAPTURE("x"), min_count=1), SAME("x")))
        found = analyse(tables)
        nodes = tables[3]
        same = [i for i, n in enumerate(nodes) if n[0] == 'SAME'][0]
        self.assertEqual(found.same[same], (0, 0))


def match_form(form):
    """
    The first (match ...) form in a read form, searched without
    recursion
    """

    pending = [form]
    while pending:
        current = pending.pop()
        if isinstance(current, pair) and current is not nil:
            if current.car is symbol("match"):
                return current
            while isinstance(current, pair) and current is not nil:
                item, current = current
                pending.append(item)
    return None


def inline_tables(source):
    """
    The tables of each arm of an inline record's match, built as
    compile/match builds them
    """

    _head, rest = match_form(Reader().read(source_str(source, "<oracle>")))
    _subject, clauses = rest
    tables = []
    while clauses is not nil:
        clause, clauses = clauses
        content = clause.car
        if isinstance(content, pair):
            tables.append(MatchCompiler().lower(
                MATCH(parse_case_pattern(content))))
    return tables


def refusal_explained(case, master, refused):
    """
    Whether a refusal rule of the oracle names the refusal
    """

    branch = {"refused": type(refused).__name__, "message": str(refused)}
    for name, predicate in RULES:
        if name in REFUSAL_RULES and predicate(case, master, branch):
            return True
    return False


class TestOracleCorpus(unittest.TestCase):
    """
    The analysis over every pattern of the oracle corpus refuses only
    where master refused or a refusal rule names the refusal. A pattern
    MatchCompiler refuses before the analysis is skipped
    """

    def check(self, case, master, tables_list, arm):
        try:
            for tables in tables_list:
                analyse(tables, arm)
        except (TypeError, ValueError) as exc:
            if "refused" in master:
                return "refused"
            if not refusal_explained(case, master, exc):
                return f"unexplained: {exc}"
            return "explained"
        return "accepted"


    def test_standalone(self):
        standalone, _inline = load_corpus()
        counts = {}
        unexplained = []
        for record in standalone["records"]:
            try:
                tables = MatchCompiler().lower(build_pattern(record["spec"]))
            except (RecursionError, ValueError):
                counts["lowering"] = counts.get("lowering", 0) + 1
                continue
            entry = record["cases"][0]
            case = {
                "kind": "standalone",
                "id": record["id"],
                "spec": record["spec"],
                "subject": entry["subject"],
            }
            outcome = self.check(case, entry["master"], [tables], False)
            if outcome.startswith("unexplained"):
                unexplained.append((record["id"], outcome))
            counts[outcome] = counts.get(outcome, 0) + 1
        self.assertEqual(unexplained, [])
        self.assertEqual(sum(counts.values()), len(standalone["records"]))
        self.assertGreater(counts.get("accepted", 0), 0)
        self.assertGreater(counts.get("explained", 0), 0)


    def test_inline(self):
        _standalone, inline = load_corpus()
        counts = {}
        unexplained = []
        for record in inline["records"]:
            tables_list = inline_tables(record["source"])
            entry = record["cases"][0]
            case = {
                "kind": "inline",
                "id": record["id"],
                "arms": record["arms"],
                "names": record["names"],
                "owners": record["owners"],
                "source": record["source"],
                "subject": entry["subject"],
            }
            outcome = self.check(case, entry["master"], tables_list, True)
            if outcome.startswith("unexplained"):
                unexplained.append((record["id"], outcome))
            counts[outcome] = counts.get(outcome, 0) + 1
        self.assertEqual(unexplained, [])
        self.assertGreater(counts.get("accepted", 0), 0)
        self.assertGreater(counts.get("explained", 0), 0)


# The end.
