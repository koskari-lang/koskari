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
Tests for the MATCH_CASE and MATCH_STORE_NAMES opcodes (runtime
match/case dispatch)

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from koskari.matcher import MatchCompiler, MATCH, CAPTURE, LITERAL, TYPE

from tests import make_callable
from tests.lib.opcode import CevalTest, build_expr

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

    Stack `[..., subject]` with the subject at TOS. EXTRA schema
    (LABEL, CONST, CONST, NOARG): a0 = no-match target PC, a1 = frozen
    matcher tuple, a2 = externs name tuple. On no match the subject is
    left on the stack and control jumps to the label; on the first match
    the subject is dropped and the capture dict is pushed. Capture names
    are declared as namespace slots (via ``local_syms``), the same
    declaration the compile/match special makes for an arm.
    """


    async def test_match_case_capture_binds_and_runs_body(self):
        """
        A matching arm drops the subject and pushes captures for
        MATCH_STORE_NAMES to bind
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["MATCH_STORE_NAMES", ("x",)],
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
        the label
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(LITERAL(10))), (), 0)],
            ["POP", 1],
            ["CONST", 111],
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
        A matching literal arm with no captures pushes an empty dict; a
        following POP discards it before the body
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(LITERAL(10))), (), 0)],
            ["POP", 1],
            ["CONST", 99],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ])
        result = await make_callable(code, ns)()
        self.assertEqual(result, 99)


    async def test_match_case_chain_first_miss_second_match(self):
        """
        A chained MATCH_CASE reuses the surviving subject: first arm
        misses and jumps, the second matches and binds
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["MATCH_CASE", ("arm2", frozen(MATCH(LITERAL(5))), (), 0)],
            ["POP", 1],
            ["CONST", 111],
            ["RETURN", 1],
            ["LABEL", "arm2"],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["MATCH_STORE_NAMES", ("x",)],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 10)


    async def test_match_case_extern_type_match(self):
        """
        MATCH_CASE loads a matcher extern (a type) from the namespace and
        matches when the subject satisfies it
        """

        f = frozen(MATCH(CAPTURE("x", TYPE("int"))))
        code, ns = build_expr([
            ["CONST", 7],
            ["MATCH_CASE", ("elsebody", f, f[2], 0)],
            ["MATCH_STORE_NAMES", f[1]],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",), values={"int": int})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 7)


    async def test_match_case_extern_type_no_match(self):
        """
        MATCH_CASE misses when the subject fails an extern type test
        """

        f = frozen(MATCH(CAPTURE("x", TYPE("int"))))
        code, ns = build_expr([
            ["CONST", "hello"],
            ["MATCH_CASE", ("elsebody", f, f[2], 0)],
            ["MATCH_STORE_NAMES", f[1]],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x",), values={"int": int})
        result = await make_callable(code, ns)()
        self.assertEqual(result, 0)


    async def test_match_case_cache_reuse_across_calls(self):
        """
        The per-site cache thaws once: a second call reuses the cached
        Matcher and returns the same result
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["MATCH_STORE_NAMES", ("x",)],
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


class TestMatchStoreNamesOpcode(CevalTest):
    """
    Tests for MATCH_STORE_NAMES, exercised through a matching MATCH_CASE
    that leaves a capture dict on the stack. The names tuple binds each
    present capture into the namespace; a name absent from the dict is
    left unbound.
    """


    async def test_match_store_names_binds_present(self):
        """
        MATCH_STORE_NAMES binds a name present in the captures and skips
        one that is absent
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["MATCH_STORE_NAMES", ("x", "y")],
            ["VAR", "x"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x", "y"))
        result = await make_callable(code, ns)()
        self.assertEqual(result, 5)


    async def test_match_store_names_skips_absent(self):
        """
        A name in the tuple but absent from the captures is left unbound;
        reading it raises NameError
        """

        code, ns = build_expr([
            ["CONST", 5],
            ["MATCH_CASE", ("elsebody", frozen(MATCH(CAPTURE("x"))), (), 0)],
            ["MATCH_STORE_NAMES", ("x", "y")],
            ["VAR", "y"],
            ["RETURN", 1],
            ["LABEL", "elsebody"],
            ["POP", 1],
            ["CONST", 0],
            ["RETURN", 1],
        ], local_syms=("x", "y"))
        with self.assertRaises(NameError):
            await make_callable(code, ns)()


# The end.
