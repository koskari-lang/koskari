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
Tests for Expression, Function, and make_callable.

Programs are expressed as lists of [opname, arg] (e.g. ["CONST", 42],
["RETURN", 1]). build_code turns these into Code; build_expr adds a
namespace and optional value bindings. make_callable(code, ns) produces
a callable; calling it returns a Coroutine. Names are bound via the
namespace (VAR); callables are not put in CONST.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""

import asyncio
import unittest

from koskari.lib import Code, Expression, Function
from koskari.types import FormalsSpec, NamespaceSpec, OpCode
from tests import make_callable


def _op(name):
    """
    Map string opcode name to OpCode enum (e.g. 'CONST' -> KOS_OP_CONST)
    """
    return getattr(OpCode, "KOS_OP_" + name)


def build_code(code, local_syms=None, name="expr"):
    """
    Build Code from a list of [opname, arg] with string opcode names.

    :param code: Sequence of [op, arg] (e.g. [["CONST", 42], ["RETURN", 1]])
    :param local_syms: Optional sequence of local names for the namespace spec
    :param name: Code name for debugging
    :return: Code instance
    """
    if local_syms is None:
        local_syms = ()
    elif not isinstance(local_syms, (tuple, list)):
        local_syms = (local_syms,)
    nspec = NamespaceSpec(None, tuple(local_syms), ())
    instr = tuple([_op(op), arg] for op, arg in code)
    return Code(nspec, instr, name)


def build_expr(code, local_syms=None, values=None, name="expr"):
    """
    Build Code and namespace; optionally bind names (for VAR) via values.

    :param code: Sequence of [op, arg] as in build_code
    :param local_syms: Optional sequence of local names (must include keys of
        values if values is provided)
    :param values: Optional dict of name -> value to store in the namespace
    :param name: Code name
    :return: (code, ns) for use with make_callable(code, ns)()
    """
    if values:
        syms = set(local_syms or ()) | set(values.keys())
        local_syms = tuple(syms)
    c = build_code(code, local_syms=local_syms, name=name)
    nspec = NamespaceSpec(None, tuple(local_syms or ()), ())
    ns = nspec.get_namespace(None)
    if values:
        for k, v in values.items():
            ns.store(k, v)
    return (c, ns)


def run_to_completion(coro):
    """
    Drive the coroutine until StopIteration; return the yielded value.

    :param coro: Coroutine from make_callable(code, ns)() or similar
    :return: The value passed to StopIteration
    """
    try:
        while True:
            coro.send(None)
    except StopIteration as e:
        return e.value


class TestExpression(unittest.TestCase):
    """
    Tests for Expression(code, ns) and make_callable(code, ns)
    """

    def test_expression_call_returns_coroutine(self):
        """
        Calling an Expression returns an awaitable coroutine
        """
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]])
        expr = make_callable(code, ns)
        coro = expr()
        self.assertTrue(hasattr(coro, 'send'))
        self.assertTrue(hasattr(coro, '__await__'))

    def test_expression_call_completes_with_value(self):
        """
        Driving the coroutine from expr() yields StopIteration with value
        """
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]])
        expr = make_callable(code, ns)
        result = run_to_completion(expr())
        self.assertEqual(result, 42)


class TestMakeCallableExpression(unittest.TestCase):
    """
    make_callable(code, ns) with no formals returns an Expression
    """

    def test_make_callable_no_formals_returns_expression(self):
        """
        make_callable(code, ns)() runs and returns value
        """
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, 42)


class TestFunction(unittest.TestCase):
    """
    Tests for Function(code, ns, formals_spec) and make_callable with formals
    """

    def test_make_callable_no_formals_is_expression(self):
        """
        make_callable(code, ns)() returns a coroutine
        """
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fn = make_callable(code, ns)
        result = run_to_completion(fn())
        self.assertEqual(result, 42)

    def test_make_callable_with_formals_binds_and_runs(self):
        """
        make_callable with formals_spec returns Function; call binds args and runs
        """
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ('y',))
        fn = make_callable(code, base_ns, fspec)
        result = run_to_completion(fn(1, y=2))
        self.assertEqual(result, 42)


class TestCodeAssembly(unittest.TestCase):
    """
    Assembly via Code.expression(parent_ns) and Code.function(formals, parent_ns).

    Same inputs as sibilant-ceval ByteCode.expression(parent) and
    ByteCode.function(formals, parent); both return callables that return
    a Coroutine when called.
    """

    def test_code_expression_parent_returns_expression(self):
        """
        Code.expression(parent_ns) returns an Expression
        """
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]])
        expr = code.expression(ns)
        self.assertIsInstance(expr, Expression)

    def test_code_expression_call_returns_coroutine(self):
        """
        Calling the result of Code.expression(ns) returns a coroutine
        """
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]])
        expr = code.expression(ns)
        coro = expr()
        self.assertTrue(hasattr(coro, 'send'))
        self.assertTrue(hasattr(coro, '__await__'))

    def test_code_expression_run_to_completion(self):
        """
        Code.expression(ns)(); drive coroutine to completion
        """
        code, ns = build_expr([["CONST", 42], ["RETURN", 1]])
        expr = code.expression(ns)
        result = run_to_completion(expr())
        self.assertEqual(result, 42)

    def test_code_function_formals_parent_returns_function(self):
        """
        Code.function(formals, parent_ns) returns a Function
        """
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ('y',))
        formals = fspec()  # FormalsSpec() => Formals with no defaults
        fn = code.function(formals, base_ns)
        self.assertIsInstance(fn, Function)

    def test_code_function_call_returns_coroutine(self):
        """
        Calling the result of Code.function(formals, parent_ns) returns a coroutine
        """
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ('y',))
        formals = fspec()
        fn = code.function(formals, base_ns)
        coro = fn(1, y=2)
        self.assertTrue(hasattr(coro, 'send'))
        self.assertTrue(hasattr(coro, '__await__'))

    def test_code_function_run_to_completion(self):
        """
        Code.function(formals, parent_ns)(*args, **kwargs); drive coroutine
        """
        code, base_ns = build_expr([["CONST", 42], ["RETURN", 1]])
        fspec = FormalsSpec(('x',), None, ('y',))
        formals = fspec()
        fn = code.function(formals, base_ns)
        result = run_to_completion(fn(1, y=2))
        self.assertEqual(result, 42)

    def test_return_two_values_sync(self):
        """
        RETURN 2 with two values on stack returns a tuple (sync, no await)
        """
        code, ns = build_expr([
            ["CONST", 42],
            ["CONST", 52],
            ["RETURN", 2],
        ])
        result = run_to_completion(make_callable(code, ns)())
        self.assertEqual(result, (42, 52))


class TestAsyncAwait(unittest.TestCase):
    """
    Async tests: await make_callable(...)() with real koskari code
    """

    def test_await_expression_add(self):
        """
        await make_callable(code, ns)() runs koskari addition and returns the result
        """

        code, ns = build_expr([
            ["CONST", 10],
            ["CONST", 32],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        coro = expr()
        result = coro.run()
        self.assertEqual(result, 42)


    def test_await_expression_add_other(self):
        """
        make_callable(code, ns)(), await it; koskari code does addition
        """

        code, ns = build_expr([
            ["CONST", 7],
            ["CONST", 35],
            ["OBJECT_ADD", 2],
            ["RETURN", 1],
        ])
        expr = make_callable(code, ns)
        coro = expr()
        result = coro.run()
        self.assertEqual(result, 42)


    def test_koskari_awaits_python_awaitable_then_adds_one(self):
        """
        Koskari code loads a callable from the namespace, calls it (Python
        async function), awaits the result, adds 1, and returns. Verifies
        the VM properly awaits and that we use namespace binding not CONST.
        """

        capture = []
        async def get_value():
            """
            Python awaitable that returns 41
            """
            capture.append(1)
            await asyncio.sleep(0)
            capture.append(2)
            return 41

        def get_value_sync():
            capture.append(3)
            return 51

        values = {
            "get_value": get_value,
            "get_value_sync": get_value_sync,
        }
        code, ns = build_expr([
            ["VAR", "get_value"],
            ["CALL", 0],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["VAR", "get_value_sync"],
            ["CALL", 0],
            ["CONST", 1],
            ["OBJECT_ADD", 2],
            ["RETURN", 2],
        ], values=values)

        expr = make_callable(code, ns)
        self.assertEqual(capture, [])

        coro = expr()
        self.assertEqual(capture, [])
        self.assertIsNotNone(coro)

        result = coro.run()
        self.assertEqual(result, (42, 52))
        self.assertEqual(capture, [1, 2, 3])


# The end.
