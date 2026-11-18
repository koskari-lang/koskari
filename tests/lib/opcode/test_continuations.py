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
Tests for delimited and undelimited continuations (RESET, SHIFT, CALL
on DelimitedContinuation, PUSH_CC, CAPTURE_CC, POP_CC) and evaluator
token.
Includes cross-context continuation pass-through tests verifying the
segmented transfer path (foreign-token KoskariContinuationApply handling).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import pytest
import sys

from koskari.exceptions import KoskariPromptNotFound
from koskari.lib import Code, FormalsSpec, NamespaceSpec, OpCode
from tests import make_callable
from tests.lib import opcode as opcode_tests
from tests.lib.opcode import CevalTest, _op


pytestmark = pytest.mark.phase_lib


_RETURN_K = [
    ["VAR", "k"],
    ["RETURN", 1],
]

_APPLY_K_5 = [
    ["VAR", "k"],
    ["CONST", 5],
    ["CALL", 1],
    ["RETURN", 1],
]


class ResetBody(list):
    """
    The rows of a reset's body, which expand_asides lays out as the
    aside the RESET row names
    """


class ShiftHandler(list):
    """
    The rows of a shift's handler, which expand_asides lays out as the
    aside the SHIFT row names, with the names of the aside's own
    namespace
    """

    def __init__(self, rows, local_syms):
        super().__init__(rows)
        self.local_syms = tuple(local_syms)


def expand_asides(code, local_syms, names=None):
    """
    Lay each reset's body and each shift's handler out the way the
    instructions take them. A RESET row carrying a ResetBody becomes
    an aside over the namespace it is written in, PUSH_ASIDE, the
    body's rows ending in their RETURN, and POP_ASIDE, then the RESET
    row naming it. A SHIFT row carrying a ShiftHandler and the name the
    continuation is bound to becomes an aside with a namespace of the
    handler's names, whose first row stores the continuation to that
    name, then the SHIFT row naming it. A body or a handler may hold
    further rows written the same way

    :param code: Sequence of [op, arg], a RESET row's second field being
        a ResetBody and a SHIFT row's a ShiftHandler
    :param local_syms: unused; the handlers' names are their asides'
    :param names: the aside names given out so far
    :return: the rows with every body and handler laid out in place
    """

    names = [] if names is None else names
    rows = []
    for op, arg in code:
        if op == "RESET" and isinstance(arg[1], ResetBody):
            name = f"aside_{len(names)}"
            names.append(name)
            rows.append(["PUSH_ASIDE", (None, name, 0, 0)])
            rows.extend(expand_asides(arg[1], local_syms, names))
            rows.append(["POP_ASIDE", 0])
            rows.append(["RESET", (arg[0], name, 0, 0)])
        elif op == "SHIFT" and isinstance(arg[1], ShiftHandler):
            name = f"aside_{len(names)}"
            names.append(name)
            nspec = NamespaceSpec("handler", arg[1].local_syms, ())
            rows.append(["PUSH_ASIDE", (nspec, name, 0, 0)])
            rows.append(["VAR_STORE", arg[2]])
            rows.extend(expand_asides(arg[1], local_syms, names))
            rows.append(["POP_ASIDE", 0])
            rows.append(["SHIFT", (arg[0], name, 0, 0)])
        else:
            rows.append([op, arg])
    return rows


def expand_with_syms(code, local_syms):
    """
    expand_asides, with local_syms as a tuple or None

    :param code: Sequence of [op, arg] as in expand_asides
    :param local_syms: None, a name, or a sequence of names
    :return: the rows, and the local names as a tuple or None
    """

    if local_syms is None:
        syms = []
    elif isinstance(local_syms, (tuple, list)):
        syms = list(local_syms)
    else:
        syms = [local_syms]
    rows = expand_asides(code, syms)
    return rows, (tuple(syms) if syms else None)


def build_code(code, local_syms=None, **kwds):
    """
    tests.lib.opcode.build_code over rows with their reset bodies and
    shift handlers laid out
    """

    rows, local_syms = expand_with_syms(code, local_syms)
    return opcode_tests.build_code(rows, local_syms=local_syms, **kwds)


def build_expr(code, local_syms=None, **kwds):
    """
    tests.lib.opcode.build_expr over rows with their reset bodies and
    shift handlers laid out
    """

    rows, local_syms = expand_with_syms(code, local_syms)
    return opcode_tests.build_expr(rows, local_syms=local_syms, **kwds)


def handler_spec(code, local_syms=("k",)):
    """
    The rows of a shift's handler, for the second field of a SHIFT
    row: they run in an aside over the code the row is in, with a
    namespace of their own in which the continuation is stored to k,
    and end in a RETURN

    :param code: Sequence of [op, arg] as in build_code
    :param local_syms: the names of the aside's namespace, k first
    :return: ShiftHandler
    """

    return ShiftHandler(code, local_syms)


def body_spec(code):
    """
    The rows of a reset's body, for the second field of a RESET row:
    they run in a frame of their own over the code and namespace the
    RESET is in, and end in a RETURN

    :param code: Sequence of [op, arg] as in build_code
    :return: ResetBody
    """

    return ResetBody(code)


class TestReset(CevalTest):
    """
    Tests for RESET: the body runs in a frame above its prompt, and
    its return, or the return of a shift's handler called above the
    same prompt, is the value of the RESET
    """

    async def test_reset_body_value(self):
        """
        A body that returns 7 with no shift: the RESET's value is 7
        """

        code = [
            ["RESET", (0, body_spec([["CONST", 7], ["RETURN", 1]]), 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        self.assertEqual(7, await make_callable(code_obj, ns)())


    async def test_reset_value_lands_on_the_caller(self):
        """
        The RESET's value goes on the caller's operands like a call's:
        (+ 1 (reset 7)) is 8
        """

        code = [
            ["CONST", 1],
            ["RESET", (0, body_spec([["CONST", 7], ["RETURN", 1]]), 0, 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        self.assertEqual(8, await make_callable(code_obj, ns)())


    async def test_reset_shift_invoke(self):
        """
        (reset (+ 1 (shift k (k 5)))) is 6: the handler's value, which
        is the copy's, is the RESET's
        """

        body = body_spec([
            ["CONST", 1],
            ["SHIFT", (0, handler_spec(_APPLY_K_5), "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code = [
            ["RESET", (0, body, 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        self.assertEqual(6, await make_callable(code_obj, ns)())


    async def test_k_applied_after_the_reset(self):
        """
        (reset (shift k k)) then (k 42): the copy's body returns 42 to
        the call
        """

        body = body_spec([
            ["SHIFT", (0, handler_spec(_RETURN_K), "k", 0)],
            ["RETURN", 1],
        ])
        code = [
            ["RESET", (0, body, 0, 0)],
            ["CONST", 42],
            ["CALL", 1],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        self.assertEqual(42, await make_callable(code_obj, ns)())


    async def test_tagged_reset(self):
        """
        A tag taken from the stack: the body's value is the RESET's
        """

        code = [
            ["VAR", "tag"],
            ["RESET", (1, body_spec([["CONST", 5], ["RETURN", 1]]), 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag": object()})
        self.assertEqual(5, await make_callable(code_obj, ns)())


    async def test_reset_count_above_1_refused_at_build(self):
        """
        The tag count is 0 or 1; building code with another fails
        """

        code = [
            ["RESET", (2, body_spec([["CONST", 5], ["RETURN", 1]]), 0, 0)],
            ["RETURN", 0],
        ]
        with self.assertRaises(ValueError) as caught:
            build_expr(code)
        self.assertEqual(
            "Code instruction 4 EXTRA field 0 2 is not 0 or 1",
            str(caught.exception))


class TestDelimitedContinuations(CevalTest):
    """
    Tests for SHIFT and CALL on a DelimitedContinuation, under RESET
    """

    async def test_delimited_continuation_cannot_be_called_from_python(self):
        """
        (reset (shift k k)) returns k; calling k from Python raises TypeError
        """

        body = body_spec([
            ["SHIFT", (0, handler_spec(_RETURN_K), "k", 0)],
            ["RETURN", 1],
        ])
        code = [
            ["RESET", (0, body, 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        cont = await expr()
        self.assertIsNotNone(cont)
        with self.assertRaises(TypeError) as ctx:
            cont(42)
        self.assertIn("cannot be called from Python", str(ctx.exception))


    async def test_var_store_then_var_call_invokes_delimited(self):
        """
        The handler stores k in a local, then VAR and CALL invoke it with
        a value. (+ 1 (shift k (do (var-store j k) (j 5)))) => 6
        """

        stored = handler_spec([
            ["VAR", "k"],
            ["VAR_STORE", "j"],
            ["VAR", "j"],
            ["CONST", 5],
            ["CALL", 1],
            ["RETURN", 1],
        ], local_syms=("k", "j"))
        body = body_spec([
            ["CONST", 1],
            ["SHIFT", (0, stored, "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code = [
            ["RESET", (0, body, 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 6)


    async def test_delimited_continuation_stored_and_invoked_outside_shift(self):
        """
        (reset (shift k k)) stores k; then (k 42) invokes it from outside
        the handler. Result 42 returns to call site.
        """

        body = body_spec([
            ["SHIFT", (0, handler_spec(_RETURN_K), "k", 0)],
            ["RETURN", 1],
        ])
        code = [
            ["RESET", (0, body, 0, 0)],
            ["VAR_STORE", "k"],
            ["VAR", "k"],
            ["CONST", 42],
            ["CALL", 1],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_delimited_result_from_outside_shift(self):
        """
        (reset (shift k k)) then (k 99) => 99. When invoking from outside
        the handler, the continuation's first op is the body's RETURN
        after SHIFT, which takes the value; the copy of the body returns
        99 to the call site.
        """

        body = body_spec([
            ["SHIFT", (0, handler_spec(_RETURN_K), "k", 0)],
            ["RETURN", 1],
        ])
        code = [
            ["RESET", (0, body, 0, 0)],
            ["VAR_STORE", "k"],
            ["VAR", "k"],
            ["CONST", 99],
            ["CALL", 1],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, local_syms=("k",))
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 99)


    async def test_tagged_shift_passes_inner_prompt_graft(self):
        """
        A shift tagged A inside a default-tag reset inside a reset
        tagged A: the handler's value k is delivered at the outer
        prompt, and invoking k from there re-enters the region through
        the inner reset, whose body's return ends its prompt.
        (+ 1 5) then (+ 6 10): 16. Delivery at the inner prompt would
        add 10 to k; an early return at the inner prompt would give 6.
        """

        tag_a = object()
        inner = body_spec([
            ["CONST", 1],
            ["CONST", tag_a],
            ["SHIFT", (1, handler_spec(_RETURN_K), "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        outer = body_spec([
            ["RESET", (0, inner, 0, 0)],
            ["CONST", 10],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code = [
            ["VAR", "tag_a"],
            ["RESET", (1, outer, 0, 0)],
            ["CONST", 5],
            ["CALL", 1],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag_a": tag_a})
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 16)


    async def test_tagged_shift_passes_inner_prompt_inplace(self):
        """
        The same shape with k invoked inside the handler: the copy
        attaches on the live chain, the inner body's return ends the
        inner prompt, and the outer body's return of 16 reaches the
        handler, which delivers it at the outer prompt. Delivery at the
        inner prompt would give 26.
        """

        tag_a = object()
        inner = body_spec([
            ["CONST", 1],
            ["CONST", tag_a],
            ["SHIFT", (1, handler_spec(_APPLY_K_5), "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        outer = body_spec([
            ["RESET", (0, inner, 0, 0)],
            ["CONST", 10],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code = [
            ["VAR", "tag_a"],
            ["RESET", (1, outer, 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag_a": tag_a})
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 16)


    async def test_shift_inside_a_copy_attached_from_a_handler(self):
        """
        A shift tagged A crosses a default-tag reset, and its handler
        invokes k. In the copy that is attached, a second shift seeks the
        default tag: it stops at the copy's inner prompt, which is region
        content and ends with its body, while the copy's outer body
        returns to the invoke site. (+ 10 5), then (+ 15 100) from the
        second handler, then (+ 1 115): 116
        """

        tag_a = object()
        apply_k_100 = handler_spec([
            ["VAR", "k"],
            ["CONST", 100],
            ["CALL", 1],
            ["RETURN", 1],
        ])
        inner = body_spec([
            ["CONST", 10],
            ["CONST", tag_a],
            ["SHIFT", (1, handler_spec(_APPLY_K_5), "k", 0)],
            ["OBJECT_ADD", 2],
            ["SHIFT", (0, apply_k_100, "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        outer = body_spec([
            ["CONST", 1],
            ["RESET", (0, inner, 0, 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code = [
            ["VAR", "tag_a"],
            ["RESET", (1, outer, 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag_a": tag_a})
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 116)


    async def test_prompt_tags_match_by_identity(self):
        """
        Two tags that compare equal are different tags: a shift seeking
        one does not stop at a prompt carrying the other, and the
        failure names the tag it sought
        """

        tag_a = [1]
        tag_b = [1]
        body = body_spec([
            ["CONST", tag_b],
            ["SHIFT", (1, handler_spec(_RETURN_K), "k", 0)],
            ["RETURN", 1],
        ])
        code = [
            ["VAR", "tag_a"],
            ["RESET", (1, body, 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag_a": tag_a})
        expr = make_callable(code_obj, ns)
        with self.assertRaises(KoskariPromptNotFound) as caught:
            await expr()
        self.assertIs(caught.exception.tag, tag_b)


    async def test_stack_tag_is_not_a_captured_operand(self):
        """
        A tag an instruction takes from the stack is gone before the
        continuation is captured: the region's one pending operand is
        the 1, so (k 5) is 6. None taken from the stack is the default
        tag, so it reaches the prompt a count of 0 made
        """

        body = body_spec([
            ["CONST", 1],
            ["CONST", None],
            ["SHIFT", (1, handler_spec(_APPLY_K_5), "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code = [
            ["RESET", (0, body, 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 6)


    async def test_none_stack_tag_makes_a_default_prompt(self):
        """
        A prompt pushed with None taken from the stack carries the
        default tag: a shift whose count is 0 captures up to it, and the
        tag is not an operand under the reset's value
        """

        body = body_spec([
            ["CONST", 1],
            ["SHIFT", (0, handler_spec(_APPLY_K_5), "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code = [
            ["VAR", "tag"],
            ["RESET", (1, body, 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code, values={"tag": None})
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 6)


    async def test_shift_count_above_1_refused_at_build(self):
        """
        A shift's count is 0 for the default tag or 1 for a tag from the
        stack; building code with any other count fails
        """

        code = [
            ["SHIFT", (2, handler_spec(_RETURN_K), "k", 0)],
            ["RETURN", 0],
        ]
        with self.assertRaises(ValueError) as caught:
            build_expr(code)
        self.assertEqual(
            "Code instruction 5 EXTRA field 0 2 is not 0 or 1",
            str(caught.exception))


class TestAsides(CevalTest):
    """
    Tests for PUSH_ASIDE and the ASIDE field: an aside is declared
    under a name, the frame it is written in passes over it, and a
    RESET, ABORT or SHIFT in the same block runs it by that name
    """

    async def test_frame_passes_over_an_aside(self):
        """
        The frame an aside is written in runs none of its rows
        """

        code = [
            ["PUSH_ASIDE", (None, "a", 0, 0)],
            ["CONST", 1],
            ["RETURN", 1],
            ["POP_ASIDE", 0],
            ["CONST", 2],
            ["RETURN", 1],
        ]
        code_obj, ns = opcode_tests.build_expr(code)
        self.assertEqual(2, await make_callable(code_obj, ns)())


    async def test_one_aside_run_by_two_rows(self):
        """
        Two RESET rows name the same body: 7 and 7 are 14
        """

        code = [
            ["PUSH_ASIDE", (None, "a", 0, 0)],
            ["CONST", 7],
            ["RETURN", 1],
            ["POP_ASIDE", 0],
            ["RESET", (0, "a", 0, 0)],
            ["RESET", (0, "a", 0, 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ]
        code_obj, ns = opcode_tests.build_expr(code)
        self.assertEqual(14, await make_callable(code_obj, ns)())


    async def test_abort_inside_a_try_block(self):
        """
        An ABORT with a try block of its own frame open around it: the
        abort leaves the block, and the handler's 5 is the reset's
        """

        code = [
            ["PUSH_ASIDE", (None, "body", 0, 0)],
            ["PUSH_TRY", "except"],
            ["PUSH_ASIDE", (None, "found", 0, 0)],
            ["CONST", 5],
            ["RETURN", 1],
            ["POP_ASIDE", 0],
            ["ABORT", (0, "found", 0, 0)],
            ["LABEL", "except"],
            ["EXCEPTION", 0],
            ["POP", 1],
            ["CONST", 42],
            ["POP_TRY", 0],
            ["RETURN", 1],
            ["POP_ASIDE", 0],
            ["RESET", (0, "body", 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = opcode_tests.build_expr(code)
        self.assertEqual(5, await make_callable(code_obj, ns)())


    async def test_softened_asides_build_again(self):
        """
        Code.soften names each aside for its extent, on the row that
        declares it and on the rows that run it, and the softened rows
        build a Code that runs the same
        """

        body = body_spec([
            ["CONST", 1],
            ["SHIFT", (0, handler_spec(_APPLY_K_5), "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code_obj, ns = build_expr([
            ["RESET", (0, body, 0, 0)],
            ["RETURN", 1],
        ])
        soft = code_obj.soften()
        named = [
            (OpCode(op).name, arg[1]) for op, arg in soft
            if OpCode(op).name in ("PUSH_ASIDE", "RESET", "SHIFT")]
        self.assertEqual([
            ("PUSH_ASIDE", "ASIDE_0"),
            ("PUSH_ASIDE", "ASIDE_1"),
            ("SHIFT", "ASIDE_1"),
            ("RESET", "ASIDE_0"),
        ], named)

        again = Code(code_obj.namespace_spec, soft, "again")
        self.assertEqual(6, await make_callable(again, ns)())


    def shift_code(self):
        """
        A Code with a reset body and a shift handler, the handler's
        aside carrying a NamespaceSpec
        """

        body = body_spec([
            ["CONST", 1],
            ["SHIFT", (0, handler_spec(_APPLY_K_5), "k", 0)],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        code_obj, ns = build_expr([
            ["RESET", (0, body, 0, 0)],
            ["RETURN", 1],
        ])
        return code_obj


    def test_soften_holds_no_reference_to_a_pool_object(self):
        """
        Softened rows that have been dropped leave the count of a pool
        object they carried where it was
        """

        code_obj = self.shift_code()
        nspec = [
            arg[0] for op, arg in code_obj.soften()
            if OpCode(op).name == "PUSH_ASIDE" and arg[0] is not None].pop()
        self.assertIsInstance(nspec, NamespaceSpec)

        before = sys.getrefcount(nspec)
        for _ in range(10):
            code_obj.soften()
        self.assertEqual(before, sys.getrefcount(nspec))


    def test_soften_holds_no_reference_to_an_aside_name(self):
        """
        An aside name taken from softened rows that have been dropped
        is held by its taker alone, as a new string is
        """

        code_obj = self.shift_code()
        name = [
            arg[1] for op, arg in code_obj.soften()
            if OpCode(op).name == "SHIFT"].pop()
        fresh = "_".join(("ASIDE", "x"))
        self.assertEqual("ASIDE_1", name)
        self.assertEqual(sys.getrefcount(fresh), sys.getrefcount(name))


    async def test_aside_run_past_its_end_raises(self):
        """
        An aside whose rows do not end in a RETURN reaches its
        POP_ASIDE, which raises
        """

        code = [
            ["PUSH_ASIDE", (None, "a", 0, 0)],
            ["CONST", 1],
            ["POP", 1],
            ["POP_ASIDE", 0],
            ["RESET", (0, "a", 0, 0)],
            ["RETURN", 1],
        ]
        code_obj, ns = opcode_tests.build_expr(code)
        with self.assertRaises(RuntimeError) as caught:
            await make_callable(code_obj, ns)()
        self.assertEqual(
            "POP_ASIDE: an aside ran past its end", str(caught.exception))


    async def test_aside_declared_twice_refused_at_build(self):
        code = [
            ["PUSH_ASIDE", (None, "a", 0, 0)],
            ["RETURN", 0],
            ["POP_ASIDE", 0],
            ["PUSH_ASIDE", (None, "a", 0, 0)],
            ["RETURN", 0],
            ["POP_ASIDE", 0],
            ["RETURN", 0],
        ]
        with self.assertRaises(ValueError) as caught:
            opcode_tests.build_expr(code)
        self.assertEqual(
            "instruction 3 declares aside 'a' again", str(caught.exception))


    async def test_undeclared_aside_refused_at_build(self):
        code = [
            ["RESET", (0, "nope", 0, 0)],
            ["RETURN", 1],
        ]
        with self.assertRaises(ValueError) as caught:
            opcode_tests.build_expr(code)
        self.assertEqual(
            "instruction 0 names aside 'nope', which no PUSH_ASIDE declares",
            str(caught.exception))


    async def test_aside_named_outside_its_block_refused_at_build(self):
        """
        An aside is run from the block it is declared in: not from
        around that block, and not from inside the aside itself
        """

        around = [
            ["PUSH_ASIDE", (None, "outer", 0, 0)],
            ["PUSH_ASIDE", (None, "inner", 0, 0)],
            ["RETURN", 0],
            ["POP_ASIDE", 0],
            ["RETURN", 0],
            ["POP_ASIDE", 0],
            ["RESET", (0, "inner", 0, 0)],
            ["RETURN", 1],
        ]
        with self.assertRaises(ValueError) as caught:
            opcode_tests.build_expr(around)
        self.assertEqual(
            "instruction 6 names aside 'inner' from outside the block it is"
            " declared in", str(caught.exception))

        within = [
            ["PUSH_ASIDE", (None, "a", 0, 0)],
            ["RESET", (0, "a", 0, 0)],
            ["RETURN", 1],
            ["POP_ASIDE", 0],
            ["RETURN", 0],
        ]
        with self.assertRaises(ValueError) as caught:
            opcode_tests.build_expr(within)
        self.assertEqual(
            "instruction 1 names aside 'a' from outside the block it is"
            " declared in", str(caught.exception))


    async def test_aside_name_must_be_str(self):
        code = [
            ["PUSH_ASIDE", (None, 3, 0, 0)],
            ["RETURN", 0],
            ["POP_ASIDE", 0],
            ["RETURN", 0],
        ]
        with self.assertRaises(TypeError) as caught:
            opcode_tests.build_expr(code)
        self.assertEqual(
            "Code instruction 0 aside name must be a str, not int",
            str(caught.exception))


class TestUndelimitedContinuations(CevalTest):
    """
    Tests for PUSH_CC, POP_CC, CAPTURE_CC (undelimited call/cc block).
    """

    async def test_call_cc_capture_and_invoke(self):
        """
        (call/cc [k] (k 42)) => 42.
        """
        code = [
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CONST", 42],
            ["CALL", 1],
            ["POP_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertEqual(result, 42)


    async def test_call_cc_invoke_with_zero_args_resumes_with_none(self):
        """
        (call/cc [k] (k)) => None.
        """
        code = [
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CALL", 0],
            ["POP_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertIsNone(result)


    async def test_call_cc_capture_returns_continuation(self):
        """
        PUSH_CC, CAPTURE_CC, POP_CC, return TOS. Result is the continuation.
        """
        code = [
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["POP_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        result = await expr()
        self.assertIsNotNone(result)
        import koskari.lib as lib
        self.assertIsInstance(result, lib.Continuation)


    async def test_capture_cc_without_push_cc_raises(self):
        """
        CAPTURE_CC with no PUSH_CC on the dynamic chain raises RuntimeError.
        """
        code = [
            ["CAPTURE_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        with self.assertRaises(RuntimeError) as ctx:
            await expr()
        self.assertIn("CAPTURE_CC", str(ctx.exception))
        self.assertIn("no CC block", str(ctx.exception))


    async def test_call_continuation_from_different_token_resumes_owner(self):
        """
        Invoking a continuation from a nested koskari context (different eval
        token) raises KoskariContinuationApply in the nested context, which
        propagates upward through the Python awaitable chain until it reaches
        the owning eval loop. The owning loop catches it in its raise
        ingress, matches the token, applies the continuation,
        and resumes normally. The result is the value passed to the
        continuation.
        """
        import koskari.lib as lib
        self.assertTrue(hasattr(lib, "KoskariContinuationApply"))

        top_code = build_code([
            ["VAR", "k"],
            ["CONST", 42],
            ["CALL", 1],
            ["RETURN", 1],
        ], local_syms=("k",), name="top")
        top_ns = NamespaceSpec(None, ("k",), ()).get_namespace(None)
        top_fn = make_callable(top_code, top_ns, FormalsSpec(("k",)))

        async def bridge(cont):
            return await top_fn(cont)

        bottom_code, bottom_ns = build_expr([
            ["VAR", "bridge"],
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CALL", 1],
            ["POP_CC", 0],
            ["LABEL", "resume"],
            ["RETURN", 1],
        ], values={"bridge": bridge})
        bottom_expr = make_callable(bottom_code, bottom_ns)

        result = await bottom_expr()
        self.assertEqual(result, 42)


    async def test_continuation_passes_through_intermediate_context_with_dyn_with_unwind(self):
        """
        Four-level call stack: owner koskari -> python -> middle koskari
        (with an active dyn-with) -> python (invokes foreign continuation).

        The KoskariContinuationApply exception must:
          1. Be intercepted by the middle koskari eval loop (foreign token).
          2. Trigger a NULL-frame unwind in the middle context, running the
             dyn-with __exit__ before the context exits.
          3. Re-raise unchanged so it propagates upward through the Python
             async bridge to the owning koskari eval loop.
          4. Be caught and applied by the owner, resuming with 42.

        Assertions:
          - result == 42 (continuation applied correctly by owner)
          - dyn-with __exit__ was called (middle context unwound cleanly)
          - __exit__ received no exception info (the exception was hidden
            from the context manager during the segmented transfer)
        """

        class Tracker:
            def __init__(self):
                self.exited = False
                self.exc_info = (None, None, None)

            def __enter__(self):
                return self

            def __exit__(self, typ, val, tb):
                self.exited = True
                self.exc_info = (typ, val, tb)
                return False

        tracker = Tracker()

        def python_invoker(cont):
            return cont(42)

        middle_nspec = NamespaceSpec(None, ("cont",), ("invoker",))
        middle_instr = tuple([_op(op), arg] for op, arg in [
            ["CONST", tracker],
            ["PUSH_DYN_WITH", None],
            ["VAR", "invoker"],
            ["VAR", "cont"],
            ["CALL", 1],
            ["POP_DYN_WITH", 0],
            ["RETURN", 1],
        ])
        middle_code = Code(middle_nspec, middle_instr, "middle")
        middle_ns = NamespaceSpec(None, ("invoker",), ()).get_namespace(None)
        middle_ns.store("invoker", python_invoker)
        middle_fn = make_callable(
            middle_code, middle_ns, FormalsSpec(("cont",)))

        async def python_bridge(cont):
            return await middle_fn(cont)

        bottom_code, bottom_ns = build_expr([
            ["VAR", "python_bridge"],
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CALL", 1],
            ["POP_CC", 0],
            ["LABEL", "resume"],
            ["RETURN", 1],
        ], values={"python_bridge": python_bridge})
        bottom_expr = make_callable(bottom_code, bottom_ns)

        result = await bottom_expr()

        self.assertEqual(result, 42)
        self.assertTrue(tracker.exited, "dyn-with __exit__ must be called during middle-context unwind")
        self.assertIsNone(tracker.exc_info[0], "dyn-with __exit__ must see no exception (hidden during segmented transfer)")


    async def test_invoking_continuation_after_eval_exits_raises_koskari_continuation_dead(self):
        """
        Capture a continuation, fully exit the koskari eval, then invoke it
        from Python. The owning context is no longer alive; expect
        KoskariContinuationDead (a normal Exception).
        """

        import koskari.lib as lib
        code = [
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["POP_CC", 0],
            ["RETURN", 1],
        ]
        code_obj, ns = build_expr(code)
        expr = make_callable(code_obj, ns)
        cont = await expr()
        self.assertIsNotNone(cont)

        with self.assertRaises(lib.KoskariContinuationDead) as ctx:
            cont(42)

        self.assertIn("no longer alive", str(ctx.exception))

        try:
            raise ctx.exception
        except Exception:
            pass


    async def test_koskari_continuation_dead_is_normal_exception(self):
        """
        KoskariContinuationDead subclasses RuntimeError so it is a normal
        catchable exception, not a BaseException.
        """

        import koskari.lib as lib
        self.assertTrue(issubclass(lib.KoskariContinuationDead, RuntimeError))
        self.assertTrue(issubclass(lib.KoskariContinuationDead, Exception))


    async def test_foreign_continuation_apply_thrown_into_gen_unwinds(self):
        """
        A KoskariContinuationApply delivered into a suspended generator via
        asend.throw() reaches the unified RAISE ingress on the revive-throw
        path and is dispatched as a continuation signal: the generator's
        local extents unwind through the cc-unwind goal (dyn-with __exit__
        runs with no exception info) and the apply re-raises unchanged so it
        can travel onward to its owner.
        """

        import koskari.lib as lib

        class Tracker:
            def __init__(self):
                self.exited = False
                self.exc_info = (None, None, None)

            def __enter__(self):
                return self

            def __exit__(self, typ, val, tb):
                self.exited = True
                self.exc_info = (typ, val, tb)
                return False

        tracker = Tracker()
        caught = []

        def steal(k):
            try:
                k(42)
            except lib.KoskariContinuationApply as e:
                caught.append(e)
            return "stolen"

        owner_code, owner_ns = build_expr([
            ["VAR", "steal"],
            ["PUSH_CC", 0],
            ["CAPTURE_CC", 0],
            ["CALL", 1],
            ["POP_CC", 0],
            ["RETURN", 1],
        ], values={"steal": steal})
        owner_result = await make_callable(owner_code, owner_ns)()
        self.assertEqual(owner_result, "stolen")
        self.assertEqual(len(caught), 1)

        other_code, other_ns = build_expr([
            ["CONST", tracker],
            ["PUSH_DYN_WITH", None],
            ["CONST", "other-ready"],
            ["YIELD", 1],
            ["POP_DYN_WITH", 0],
            ["RETURN", 0],
        ])
        other = make_callable(other_code, other_ns)()
        ready = await other.__anext__()
        self.assertEqual(ready, "other-ready")

        asend = other.__anext__()
        with self.assertRaises(lib.KoskariContinuationApply):
            await asend.throw(lib.KoskariContinuationApply, caught[0])

        self.assertTrue(
            tracker.exited,
            "dyn-with __exit__ must run during foreign-apply unwind")
        self.assertIsNone(
            tracker.exc_info[0],
            "dyn-with __exit__ must see no exception info")


class TestEvaluatorToken(CevalTest):
    """
    Tests for KosEvaluatorToken and exception types from evaluator-token model
    """

    async def test_evaluator_token_and_exceptions_exist_on_module(self):
        """
        koskari.lib exposes EvaluatorToken, DelimitedContinuation,
        Continuation, and exception types
        """
        import koskari.lib as lib
        self.assertTrue(hasattr(lib, "EvaluatorToken"))
        self.assertTrue(hasattr(lib, "DelimitedContinuation"))
        self.assertTrue(hasattr(lib, "Continuation"))
        self.assertTrue(hasattr(lib, "KoskariContinuationDead"))
        self.assertTrue(hasattr(lib, "KoskariContinuationApply"))
        self.assertTrue(hasattr(lib, "KoskariContinuationError"))


# The end.
