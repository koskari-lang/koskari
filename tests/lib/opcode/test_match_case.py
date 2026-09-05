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
Tests for the MATCH_CASE opcode (runtime match/case dispatch)

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.matcher import (
    MatchCompiler, MATCH, CAPTURE, LITERAL, ONEOF, OPTIONAL, TYPE)

from koskari.lib import CollectorNamespace, Namespace

from tests import make_callable
from tests.lib.opcode import CevalTest, build_code, build_expr

import pytest

pytestmark = pytest.mark.phase_lib


def frozen(pattern):
    """
    Freeze a pattern into its matcher 6-tuple for embedding as a const
    """

    return MatchCompiler().lower(pattern)


class TestMatchCaseOpcode(CevalTest):
    """
    Tests for MATCH_CASE.

    Stack `[..., subject, e0..eN-1]` with N = len(a2). EXTRA schema
    (LABEL, CONST, CONST, COUNT): a0 = no-match target PC, a1 = frozen
    matcher tuple, a2 = missing-externs name tuple, whose values are
    pushed above the subject in extern-table order; extern names absent
    from a2 resolve from the matcher extern defaults; a3 = keep-subject
    flag. The stacked externs are consumed on both outcomes. On no match
    the subject is left on the stack and control jumps to the label; on
    the first match the subject is dropped (unless kept), the arm's
    captures are bound into the namespace, and nothing is pushed.
    Capture names are declared as namespace slots (via ``local_syms``),
    the same declaration the compile/match special makes for an arm.
    """


    async def test_match_case_capture_binds_and_runs_body(self):
        """
        A matching arm drops the subject and binds its capture
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)


    async def test_match_case_no_match_jumps_leaving_subject(self):
        """
        A non-matching arm leaves the subject on the stack and jumps to
        the no-match label
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(LITERAL(10))), (), 0)],
            ["POP", 1],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_match_case_literal_match_no_captures(self):
        """
        A capture-less match consumes the subject and pushes nothing, so
        the body runs on the stack beneath it
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(LITERAL(10))), (), 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 1)


    async def test_match_case_chain_first_miss_second_match(self):
        """
        A chained MATCH_CASE reuses the surviving subject: first arm
        misses (subject left), second arm captures it
        """

        code, ns = build_expr([
            ["CONST", 7],
            ["MATCH_CASE", ("arm2", frozen(MATCH(LITERAL(5))), (), 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "arm2"],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 7)


    async def test_match_case_extern_type_match(self):
        """
        MATCH_CASE consumes a stacked matcher extern (a type) and
        matches a subject of that type
        """

        f = frozen(MATCH(CAPTURE("x", TYPE("mytype"))))
        code, ns = build_expr([
            ["CONST", 5],
            ["VAR", "mytype"],
            ["MATCH_CASE", ("elsebody", f, f[2], 0)],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x", "mytype"), values={"mytype": int})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)


    async def test_match_case_extern_type_no_match(self):
        """
        MATCH_CASE misses when the subject fails a stacked extern type
        and the stacked extern is consumed, leaving the subject
        """

        f = frozen(MATCH(CAPTURE("x", TYPE("mytype"))))
        code, ns = build_expr([
            ["CONST", "notanint"],
            ["VAR", "mytype"],
            ["MATCH_CASE", ("elsebody", f, f[2], 0)],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["RETURN", 1],
        ], local_syms=("x", "mytype"), values={"mytype": int})
        result = await make_callable(code, ns)()
        self.assertEqual(result, "notanint")


    async def test_match_case_cache_reuse_across_calls(self):
        """
        The thawed matcher and the resolved slot indices are cached per
        site; a second call through the same code binds the same way
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        fn = make_callable(code, ns)
        self.assertEqual(await fn(), 5)
        self.assertEqual(await fn(), 5)


    async def test_match_case_binds_present_and_skips_absent(self):
        """
        Only the slots that fired are bound: the other branch of a
        ONEOF is left unbound and raises NameError when read
        """

        pat = MATCH(ONEOF(CAPTURE("x", LITERAL(5)), CAPTURE("y")))
        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(pat), (), 0)],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x", "y"))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(pat), (), 0)],
            ["VAR", "y"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x", "y"))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_match_case_optional_unfired_stays_unbound(self):
        """
        An OPTIONAL capture that did not fire leaves its name unbound
        """

        pat = MATCH(CAPTURE("x"), OPTIONAL(CAPTURE("y", LITERAL(1))))
        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(pat), (), 0)],
            ["VAR", "y"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x", "y"))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


    async def test_match_case_binds_name_without_fast_slot(self):
        """
        A capture whose name has no fast slot in the namespace is
        bound by name: a collector namespace declares nothing up front
        and takes the binding on the store
        """

        code = build_code([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        ns = CollectorNamespace("cn", None, (), ())
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)
        self.assertEqual(ns["x"], 5)


    async def test_match_case_rebinds_under_another_layout(self):
        """
        One code object run under namespaces of different layouts binds
        the capture into the right slot each time: a same-contents
        layout under another tuple is adopted, a different layout is
        re-resolved
        """

        code = build_code([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["CONST", None],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))

        first = Namespace("first", None, ("x",))
        await make_callable(code, first)()
        self.assertEqual(first["x"], 5)

        sibling = Namespace("sibling", None, ("x",))
        await make_callable(code, sibling)()
        self.assertEqual(sibling["x"], 5)

        shifted = Namespace("shifted", None, ("y", "x"))
        await make_callable(code, shifted)()
        self.assertEqual(shifted["x"], 5)
        with self.assertRaises(NameError):
            shifted["y"]

        await make_callable(code, first)()
        self.assertEqual(first["x"], 5)


class TestMatchCaseKeepSubject(CevalTest):
    """
    Tests for the keep-subject flag (EXTRA a3). With keep set, a match
    leaves the subject on the stack after binding, so a guard can later
    advance (subject intact) or POP it to commit. A no-match is
    unchanged: subject left, jump taken.
    """


    async def test_keep_match_leaves_subject(self):
        """
        keep=1 match binds and leaves the subject as TOS
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 1)],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)


    async def test_keep_match_still_binds_captures(self):
        """
        keep=1 binding works as usual; dropping the retained subject
        then reading the bound name yields the capture
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 1)],
            ["POP", 1],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)


    async def test_keep_no_match_jumps_leaving_subject(self):
        """
        keep=1 no-match behaves like keep=0: subject left, jump taken
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(LITERAL(10))), (), 1)],
            ["POP", 1],
            ["CONST", 1],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)


# The end.
