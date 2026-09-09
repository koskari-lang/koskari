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
Bytecode dispatch tests for values as a KosCallable install target

Exercises the install/vinstall fast path: calling a values instance from
koskari bytecode merges stored + invocation args and flattens into the
evaluator (inverse of KosPartial).

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.lib import BoundMethod, FormalsSpec, demand_call, values
from tests import make_callable
from tests.lib.opcode import build_code, build_expr


pytestmark = pytest.mark.phase_lib


def _pyadd(a, b):
    return a + b


def _pycollect(*args, **kwargs):
    return (args, kwargs)


def _make_concat_fn():
    code, base_ns = build_expr([
        ["VAR", "a"],
        ["VAR", "b"],
        ["OBJECT_ADD", 2],
        ["RETURN", 1],
    ])
    fspec = FormalsSpec(("a", "b"), None, ())
    return code.function(fspec(), base_ns)


def _make_add_one_method():
    code, base_ns = build_expr([
        ["VAR", "self"],
        ["CONST", 1],
        ["OBJECT_ADD", 2],
        ["RETURN", 1],
    ])
    fspec = FormalsSpec(("self",), None, ())
    return code.function(fspec(), base_ns)


def _call_through_bytecode(callable_obj, *args, tail=False):
    """
    Run a koskari function that calls callable_obj with args via CALL or
    TAIL_CALL, driven synchronously through demand_call.

    :param callable_obj: Callable placed in the outer closure as target
    :param args: Positional CONST args pushed before the call
    :param tail: If true, emit TAIL_CALL instead of CALL
    :return: The call result
    """

    _, outer_ns = build_expr(
        [["CONST", 0], ["RETURN", 1]],
        local_syms=("target",),
        values={"target": callable_obj},
    )

    body = [["FAST", 0]]
    for a in args:
        body.append(["CONST", a])
    op = "TAIL_CALL" if tail else "CALL"
    body.append([op, len(args)])
    if not tail:
        body.append(["RETURN", 1])

    outer_code = build_code(
        body,
        local_syms=(),
        closure_syms=("target",),
        name="call_values",
    )
    outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())
    return demand_call(outer_fn)


def _call_names_through_bytecode(callable_obj, pos_args, kwd_names, kwd_vals):
    """
    CALL_NAMES path: [callable, pos..., kwd_vals..., names]

    :return: The call result
    """

    _, outer_ns = build_expr(
        [["CONST", 0], ["RETURN", 1]],
        local_syms=("target",),
        values={"target": callable_obj},
    )

    body = [["FAST", 0]]
    for a in pos_args:
        body.append(["CONST", a])
    for v in kwd_vals:
        body.append(["CONST", v])
    body.append(["CONST", tuple(kwd_names)])
    body.append(["CALL_NAMES", len(pos_args)])
    body.append(["RETURN", 1])

    outer_code = build_code(
        body,
        local_syms=(),
        closure_syms=("target",),
        name="call_values_names",
    )
    outer_fn = make_callable(outer_code, outer_ns, FormalsSpec())
    return demand_call(outer_fn)


class TestValuesDispatch(unittest.TestCase):
    """
    Calling a values instance from koskari bytecode routes through install
    """

    def test_kosfunction_stored_args(self):
        """
        values("A")(kos_fn, "B") via bytecode merges to kos_fn("A", "B")
        """

        v = values("A")
        self.assertEqual(_call_through_bytecode(v, _make_concat_fn(), "B"),
                         "AB")


    def test_kosfunction_invocation_only(self):
        """
        Empty values forwards invocation args to the KosFunction target
        """

        v = values()
        self.assertEqual(
            _call_through_bytecode(v, _make_concat_fn(), "A", "B"),
            "AB")


    def test_kosfunction_tail(self):
        """
        TAIL_CALL of values into a KosFunction returns the callee result
        """

        v = values("A")
        self.assertEqual(
            _call_through_bytecode(v, _make_concat_fn(), "B", tail=True),
            "AB")


    def test_bound_method(self):
        """
        values() applied to a KosBoundMethod prepends the bound self
        """

        fn = _make_add_one_method()
        bm = fn.__get__(41, type(41))
        self.assertIsInstance(bm, BoundMethod)
        v = values()
        self.assertEqual(_call_through_bytecode(v, bm), 42)


    def test_plain_python(self):
        """
        values over a plain Python callable uses the Case B full call path
        """

        v = values("A")
        self.assertEqual(_call_through_bytecode(v, _pyadd, "B"), "AB")


    def test_plain_python_tail(self):
        """
        TAIL_CALL of values into a plain Python callable
        """

        v = values("A")
        self.assertEqual(
            _call_through_bytecode(v, _pyadd, "B", tail=True),
            "AB")


    def test_builtin(self):
        """
        values applied to a builtin (max) via bytecode
        """

        v = values(1, 5, 3)
        self.assertEqual(_call_through_bytecode(v, max), 5)


    def test_keyword_clash_invocation_wins(self):
        """
        Stored and invocation keywords merge; invocation wins on clash
        """

        v = values(1, x=10, y=20)
        result = _call_names_through_bytecode(
            v, (_pycollect,), ("x", "z"), (99, 30))
        self.assertEqual(result, ((1,), {"x": 99, "y": 20, "z": 30}))


    def test_empty_invocation_typeerror(self):
        """
        Calling values with zero positionals raises TypeError on bytecode path
        """

        v = values(1, 2)
        with self.assertRaises(TypeError):
            _call_through_bytecode(v)


    def test_parity_tp_call_and_bytecode(self):
        """
        Same call via tp_call and via bytecode yields identical results
        """

        v = values(1, 2, x=10)
        py_result = v(_pycollect, 3, y=20)
        bc_result = _call_names_through_bytecode(
            v, (_pycollect, 3), ("y",), (20,))
        self.assertEqual(py_result, bc_result)


class TestValuesTCOAndFrames(unittest.TestCase):
    """
    Tail-call and dangling-frame guards for the values install path
    """

    def test_tail_recursion_via_values(self):
        """
        Tail-position values-call into a self-recursive KosFunction does not
        grow the frame stack (deep countdown completes)
        """

        # tick(n): if n <= 0 return n; else (values() tick (- n 1)) via TAIL_CALL
        # FAST layout: 0=n (local), 1=tick (closure), 2=vals (closure)
        tick_body = [
            ["FAST", 0],
            ["CONST", 0],
            ["OBJECT_LE", 2],
            ["JUMP_FALSE", "recur"],
            ["FAST", 0],
            ["RETURN", 1],
            ["LABEL", "recur"],
            ["FAST", 2],
            ["FAST", 1],
            ["FAST", 0],
            ["CONST", 1],
            ["OBJECT_SUBTRACT", 2],
            ["TAIL_CALL", 2],
        ]
        vals = values()
        tick_code = build_code(
            tick_body,
            local_syms=("n",),
            closure_syms=("tick", "vals"),
            name="tick",
        )
        _, base_ns = build_expr(
            [["CONST", 0], ["RETURN", 1]],
            local_syms=("tick", "vals"),
            values={"tick": None, "vals": vals},
        )
        tick = make_callable(tick_code, base_ns, FormalsSpec(("n",), None, ()))
        base_ns.store("tick", tick)

        depth = 5000
        self.assertEqual(demand_call(tick, depth), 0)


    def test_nontail_python_loop_no_dangling_frame(self):
        """
        Non-tail Case B leaves no dangling frame across many values calls
        """

        v = values(1)
        for _ in range(500):
            self.assertEqual(_call_through_bytecode(v, _pyadd, 2), 3)


# The end.
