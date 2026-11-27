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
tests.reproducers.test_throw_context

Full-stack reproducer: an exception thrown into a generator is raised
at the generator's yield and chained there to the exception the
generator handles, as CPython chains a throw, whether the generator is
stepped from Python, thrown into from Koskari, or reached through a
yield-from, and whether its yield is inside a dyn-wind or a dyn-with
that the step enters again first. A before segment or an __enter__
that raises while a throw re-enters the generator raises from the
thrown exception. Each expectation is CPython's own, taken from the
equivalent Python generator on the running version

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import os
import sys
import unittest

import pytest

from koskari.module import init_module, load_module_async, new_module


pytestmark = pytest.mark.phase_reproducers

_SCRIPT = os.path.join(
    os.path.dirname(__file__), 'scripts', 'throw_context.ki')


# The Koskari generators, each with the Python generator whose yield
# has the same handled exception
SHAPES = (
    ('plain', 'plain'),
    ('handling', 'handling'),
    ('wind_plain', 'plain'),
    ('wind_inside', 'handling'),
    ('wind_outside', 'handling'),
    ('with_plain', 'plain'),
    ('with_inside', 'handling'),
)


def handled(tag):
    """
    The handled exception a segment sees, tagged
    """

    return (tag, repr(sys.exc_info()[1]))


def check(fail):
    """
    Raise as each entry in fail asks: 'raise' a plain error, 'from' one
    with an explicit cause, 'none' one raised from None, or an exception
    instance itself
    """

    for mode in fail:
        if mode == 'raise':
            raise RuntimeError("wind-error")
        if mode == 'from':
            raise RuntimeError("wind-error") from OSError("explicit")
        if mode == 'none':
            raise RuntimeError("wind-error") from None
        if isinstance(mode, BaseException):
            raise mode


class Manager:
    """
    A context manager that logs the handled exception its enter and
    its exit see, and whose enter raises as fail asks
    """

    def __init__(self, log, fail):
        self.log = log
        self.fail = fail


    def __enter__(self):
        self.log.append(handled("enter"))
        check(self.fail)
        return self


    def __exit__(self, exc_type, exc, tb):
        self.log.append(handled("exit"))
        return False


class AsyncManager:
    """
    A context manager whose enter returns an awaitable, which suspends
    once and then raises as fail asks
    """

    def __init__(self, log, fail):
        self.log = log
        self.fail = fail


    async def entered(self):
        await asyncio.sleep(0)
        self.log.append(handled("enter"))
        check(self.fail)
        return self


    def __enter__(self):
        return self.entered()


    def __exit__(self, exc_type, exc, tb):
        self.log.append(handled("exit"))
        return False


def step(awaitable, suspensions=None):
    """
    Step an awaitable from Python to its end, counting each suspension
    into suspensions when given

    :return: its value
    """

    try:
        while True:
            awaitable.send(None)
            if suspensions is not None:
                suspensions.append(1)
    except StopIteration as done:
        return done.value


def describe(exc):
    """
    What a raised exception carries, comparable across two objects
    """

    return (
        type(exc).__name__, str(exc), repr(exc.__context__),
        repr(exc.__cause__), exc.__suppress_context__)


async def py_plain():
    yield 1
    yield 2


async def py_handling():
    try:
        raise KeyError("gen-handled")
    except KeyError:
        yield 1
        yield 2


async def py_throw_from(g, exc, in_handler):
    if in_handler:
        try:
            raise IndexError("thrower-handled")
        except IndexError:
            return await g.athrow(exc)
    return await g.athrow(exc)


async def py_close_from(g, in_handler):
    if in_handler:
        try:
            raise IndexError("thrower-handled")
        except IndexError:
            return await g.aclose()
    return await g.aclose()


async def py_closer_plain(seen):
    try:
        yield 1
    except GeneratorExit as e:
        seen.append(repr(e.__context__))
        raise


async def py_closer_handling(seen):
    try:
        raise KeyError("gen-handled")
    except KeyError:
        try:
            yield 1
        except GeneratorExit as e:
            seen.append(repr(e.__context__))
            raise


def c_inner_plain(seen):
    try:
        yield 1
        yield 2
    except ValueError as e:
        seen.append(repr(e.__context__))
        raise


def c_inner_handling(seen):
    try:
        raise KeyError("inner-handled")
    except KeyError:
        try:
            yield 1
            yield 2
        except ValueError as e:
            seen.append(repr(e.__context__))
            raise


def c_delegate_plain(inner):
    yield from inner


def c_delegate_handling(inner):
    try:
        raise OSError("outer-handled")
    except OSError:
        yield from inner


async def py_inner_plain(seen):
    try:
        yield 1
        yield 2
    except ValueError as e:
        seen.append(repr(e.__context__))
        raise


async def py_inner_handling(seen):
    try:
        raise KeyError("inner-handled")
    except KeyError:
        try:
            yield 1
            yield 2
        except ValueError as e:
            seen.append(repr(e.__context__))
            raise


def c_inner_reraise(seen):
    try:
        yield 1
        yield 2
    except ValueError:
        raise RuntimeError("inner-raised")


def c_inner_returns(seen):
    try:
        yield 1
        yield 2
    except ValueError:
        seen.append("caught")


def c_inner_send_raises(seen):
    yield 1
    raise RuntimeError("inner-raised")


def c_inner_closes(seen):
    try:
        yield 1
        yield 2
    except GeneratorExit as e:
        seen.append(repr(e.__context__))
        raise


def c_continuing_plain(inner, seen):
    yield from inner
    yield "after"


def c_continuing_handling(inner, seen):
    try:
        raise OSError("outer-handled")
    except OSError:
        try:
            yield from inner
        except GeneratorExit as e:
            seen.append(repr(e.__context__))
            raise
        yield "after"


def outcome(fn):
    """
    What calling fn comes to: its value, or what it raised
    """

    try:
        return ('value', fn())
    except BaseException as exc:
        return describe(exc)


def in_handler_call(in_handler, fn, *args):
    """
    Call fn from Python, inside an except block when in_handler
    """

    if in_handler:
        try:
            raise IndexError("thrower-handled")
        except IndexError:
            return fn(*args)
    return fn(*args)


def raised(fn, *args):
    """
    The exception fn raises, or None
    """

    try:
        fn(*args)
    except BaseException as exc:
        return exc
    return None


class ThrowContextBase(unittest.TestCase):
    """
    Loads the script with its Python helpers
    """

    def setUp(self):
        mod = new_module('tests.reproducers.throw_context')
        init_module(mod, os.path.abspath(_SCRIPT))
        vars(mod).update(
            handled=handled, check=check, Manager=Manager,
            AsyncManager=AsyncManager)
        asyncio.run(load_module_async(mod))
        self.mod = mod


    def started(self, name, fail=None):
        """
        A Koskari generator by name, stepped to its first yield, with
        its log emptied
        """

        log = []
        fail = [] if fail is None else fail
        gen = getattr(self.mod, name)(log, fail)
        step(gen.__anext__())
        del log[:]
        return gen, log, fail


def py_started(name):
    """
    A Python generator by name, stepped to its first yield
    """

    gen = {'plain': py_plain, 'handling': py_handling}[name]()
    step(gen.__anext__())
    return gen


class TestThrowContext(ThrowContextBase):
    """
    A throw carries the generator's handled exception at its yield as
    __context__, and never the thrower's
    """

    def assert_route(self, throw_kos, throw_py, in_handler):
        for name, py_name in SHAPES:
            with self.subTest(shape=name, in_handler=in_handler):
                gen, _log, _fail = self.started(name)
                exc = ValueError("thrown")
                got = raised(throw_kos, gen, exc, in_handler)
                self.assertIs(got, exc)

                py_exc = ValueError("thrown")
                want = raised(
                    throw_py, py_started(py_name), py_exc, in_handler)
                self.assertIs(want, py_exc)

                self.assertEqual(describe(got), describe(want))
                self.assertIsNone(got.__cause__)
                self.assertFalse(got.__suppress_context__)


    def test_python_athrow(self):
        def throw(gen, exc, in_handler):
            in_handler_call(in_handler, lambda: step(gen.athrow(exc)))

        self.assert_route(throw, throw, False)
        self.assert_route(throw, throw, True)


    def test_koskari_athrow_outside_a_handler(self):
        def throw_kos(gen, exc, in_handler):
            step(self.mod.throw_from(gen, exc, in_handler))

        def throw_py(gen, exc, in_handler):
            step(py_throw_from(gen, exc, in_handler))

        self.assert_route(throw_kos, throw_py, False)


    def test_koskari_athrow_inside_a_handler(self):
        def throw_kos(gen, exc, in_handler):
            step(self.mod.throw_from(gen, exc, in_handler))

        def throw_py(gen, exc, in_handler):
            step(py_throw_from(gen, exc, in_handler))

        self.assert_route(throw_kos, throw_py, True)


    def test_type_form_athrow(self):
        for name, py_name in SHAPES:
            with self.subTest(shape=name):
                gen, _log, _fail = self.started(name)
                got = raised(lambda: step(gen.athrow(ValueError)))
                want = raised(
                    lambda: step(py_started(py_name).athrow(ValueError)))
                self.assertIs(type(want), type(got))
                self.assertEqual(describe(got), describe(want))


    def test_segments_see_the_same_handled_exception_on_throw_and_send(self):
        for name in (
                'wind_plain', 'wind_inside', 'wind_outside',
                'with_plain', 'with_inside'):
            for in_handler in (False, True):
                with self.subTest(shape=name, in_handler=in_handler):
                    gen, log, _fail = self.started(name)
                    step(self.mod.send_from(gen, in_handler))
                    sent = log[0]

                    gen, log, _fail = self.started(name)
                    raised(lambda: step(self.mod.throw_from(
                        gen, ValueError("thrown"), in_handler)))
                    self.assertEqual(log[0], sent)

                    gen, log, _fail = self.started(name)
                    step(in_handler_call(in_handler, gen.asend, None))
                    sent = log[0]

                    gen, log, _fail = self.started(name)
                    raised(lambda: in_handler_call(
                        in_handler,
                        lambda: step(gen.athrow(ValueError("thrown")))))
                    self.assertEqual(log[0], sent)


class TestDelegateThrowContext(ThrowContextBase):
    """
    A throw into a generator delegating through yield-from goes to the
    delegate as it is and is chained at the delegate's yield, as
    CPython's yield from passes a throw on
    """

    def inner(self, source, name, seen):
        """
        The delegate by its source: a Koskari generator, or a Python
        generator or async generator
        """

        if source == 'koskari':
            return getattr(self.mod, f"inner_{name}")(seen)
        if source == 'python':
            return globals()[f"c_inner_{name}"](seen)
        return globals()[f"py_inner_{name}"](seen)


    def assert_delegate(self, outer, source='koskari'):
        for inner in ('plain', 'handling'):
            for in_handler in (False, True):
                for route in ('python', 'koskari'):
                    with self.subTest(
                            inner=inner, in_handler=in_handler, route=route):
                        seen = []
                        gen = getattr(self.mod, f"delegate_{outer}")(
                            self.inner(source, inner, seen))
                        step(gen.__anext__())
                        exc = ValueError("thrown")
                        if route == 'python':
                            got = raised(lambda: in_handler_call(
                                in_handler,
                                lambda: step(gen.athrow(exc))))
                        else:
                            got = raised(lambda: step(
                                self.mod.throw_from(gen, exc, in_handler)))

                        py_seen = []
                        py_gen = globals()[f"c_delegate_{outer}"](
                            globals()[f"c_inner_{inner}"](py_seen))
                        next(py_gen)
                        py_exc = ValueError("thrown")
                        want = raised(lambda: in_handler_call(
                            in_handler, py_gen.throw, py_exc))

                        self.assertIs(got, exc)
                        self.assertEqual(seen, py_seen)
                        self.assertEqual(describe(got), describe(want))


    def test_delegate_outside_a_handler(self):
        self.assert_delegate('plain')


    def test_delegate_inside_a_handler(self):
        self.assert_delegate('handling')


    def test_python_generator_delegate(self):
        self.assert_delegate('plain', 'python')
        self.assert_delegate('handling', 'python')


    def test_python_async_generator_delegate(self):
        self.assert_delegate('plain', 'python_async')
        self.assert_delegate('handling', 'python_async')


class TestDelegateOutcomes(ThrowContextBase):
    """
    What comes back out of a delegate: a different exception raised for
    a throw, a return after a throw, a failing send, and a close, each
    as CPython's yield from has it
    """

    def assert_outcome(self, inner, act):
        for outer in ('plain', 'handling'):
            with self.subTest(outer=outer, inner=inner):
                seen = []
                gen = getattr(self.mod, f"continuing_{outer}")(
                    getattr(self.mod, f"inner_{inner}")(seen), seen)
                step(gen.__anext__())
                got = outcome(lambda: step(act(gen, False)))

                py_seen = []
                py_gen = globals()[f"c_continuing_{outer}"](
                    globals()[f"c_inner_{inner}"](py_seen), py_seen)
                next(py_gen)
                want = outcome(lambda: act(py_gen, True))

                self.assertEqual(got, want)
                self.assertEqual(seen, py_seen)


    def test_delegate_raises_another_exception_for_a_throw(self):
        self.assert_outcome(
            'reraise',
            lambda g, sync: (g.throw if sync else g.athrow)(
                ValueError("thrown")))


    def test_delegate_returns_after_a_throw(self):
        self.assert_outcome(
            'returns',
            lambda g, sync: (g.throw if sync else g.athrow)(
                ValueError("thrown")))


    def test_delegate_raises_for_a_send(self):
        self.assert_outcome(
            'send_raises',
            lambda g, sync: g.send(None) if sync else g.asend(None))


    def test_close_through_a_delegation(self):
        self.assert_outcome(
            'closes',
            lambda g, sync: g.close() if sync else g.aclose())


class TestCloseContext(ThrowContextBase):
    """
    The GeneratorExit of a close is chained as CPython chains it
    """

    def test_aclose(self):
        for name in ('closer_plain', 'closer_handling'):
            for in_handler in (False, True):
                for route in ('python', 'koskari'):
                    with self.subTest(
                            shape=name, in_handler=in_handler, route=route):
                        seen = []
                        gen = getattr(self.mod, name)(seen)
                        step(gen.__anext__())
                        py_seen = []
                        py_gen = globals()[f"py_{name}"](py_seen)
                        step(py_gen.__anext__())

                        if route == 'python':
                            in_handler_call(
                                in_handler, lambda: step(gen.aclose()))
                            in_handler_call(
                                in_handler, lambda: step(py_gen.aclose()))
                        else:
                            step(self.mod.close_from(gen, in_handler))
                            step(py_close_from(py_gen, in_handler))

                        self.assertEqual(seen, py_seen)


class TestWindErrorOnThrow(ThrowContextBase):
    """
    A before segment or an __enter__ that raises while a throw enters
    the generator again raises from the thrown exception
    """

    def throws(self, route, gen, exc):
        if route == 'python':
            return raised(lambda: step(gen.athrow(exc)))
        return raised(lambda: step(self.mod.throw_from(gen, exc, False)))


    def test_raises_from_the_thrown_exception(self):
        for name in (
                'wind_plain', 'wind_outside', 'wind_slow', 'with_plain'):
            for route in ('python', 'koskari'):
                with self.subTest(shape=name, route=route):
                    gen, log, fail = self.started(name)
                    fail.append('raise')
                    exc = ValueError("thrown")
                    err = self.throws(route, gen, exc)
                    self.assertIs(type(err), RuntimeError)
                    self.assertIs(err.__cause__, exc)
                    self.assertTrue(err.__suppress_context__)
                    self.assertEqual(repr(err.__context__), log[0][1])


    def test_explicit_cause_stands(self):
        for route in ('python', 'koskari'):
            with self.subTest(route=route):
                gen, _log, fail = self.started('wind_plain')
                fail.append('from')
                err = self.throws(route, gen, ValueError("thrown"))
                self.assertIs(type(err), RuntimeError)
                self.assertEqual(
                    repr(err.__cause__), repr(OSError("explicit")))
                self.assertTrue(err.__suppress_context__)


    def test_cause_from_none_stands(self):
        for route in ('python', 'koskari'):
            with self.subTest(route=route):
                gen, _log, fail = self.started('wind_plain')
                fail.append('none')
                err = self.throws(route, gen, ValueError("thrown"))
                self.assertIs(type(err), RuntimeError)
                self.assertIsNone(err.__cause__)
                self.assertTrue(err.__suppress_context__)


    def test_thrown_exception_raised_again_links_nothing(self):
        for route in ('python', 'koskari'):
            with self.subTest(route=route):
                gen, _log, fail = self.started('wind_plain')
                exc = ValueError("thrown")
                fail.append(exc)
                err = self.throws(route, gen, exc)
                self.assertIs(err, exc)
                self.assertIsNone(err.__cause__)


    def test_send_links_nothing(self):
        for name in ('wind_plain', 'with_plain'):
            with self.subTest(shape=name):
                gen, _log, fail = self.started(name)
                fail.append('raise')
                err = raised(lambda: step(gen.asend(None)))
                self.assertIs(type(err), RuntimeError)
                self.assertIsNone(err.__cause__)
                self.assertFalse(err.__suppress_context__)


    def test_awaited_enter_raises_from_the_thrown_exception(self):
        for route in ('python', 'koskari'):
            with self.subTest(route=route):
                gen, log, fail = self.started('with_async')
                fail.append('raise')
                exc = ValueError("thrown")
                suspensions = []
                if route == 'python':
                    err = raised(lambda: step(gen.athrow(exc), suspensions))
                else:
                    err = raised(lambda: step(
                        self.mod.throw_from(gen, exc, False), suspensions))
                self.assertEqual(len(suspensions), 1)
                self.assertIs(type(err), RuntimeError)
                self.assertIs(err.__cause__, exc)
                self.assertTrue(err.__suppress_context__)
                self.assertEqual(repr(err.__context__), log[-1][1])


    def test_continuation_signal_links_nothing(self):
        """
        A before segment that calls a continuation while a throw enters
        the generator again leaves with the signal, which reaches its
        owner with no cause
        """

        signals = []

        def helper(fn, log, k):
            flags = []
            gen = fn(log, k, flags)
            step(gen.__anext__())
            flags.append('escape')
            try:
                step(gen.athrow(ValueError("thrown")))
            except BaseException as signal:
                signals.append(signal.__cause__)
                raise

        with self.assertRaises(StopIteration) as done:
            self.mod.owner([], helper, self.mod.escaping).send(None)
        self.assertEqual(done.exception.value[-1], "escaped")
        self.assertEqual(signals, [None])


    def test_aclose_raises_from_the_generator_exit(self):
        for name in ('wind_plain', 'with_plain'):
            with self.subTest(shape=name):
                gen, _log, fail = self.started(name)
                fail.append('raise')
                err = raised(lambda: step(gen.aclose()))
                self.assertIs(type(err), RuntimeError)
                self.assertIsInstance(err.__cause__, GeneratorExit)
                self.assertTrue(err.__suppress_context__)


# The end.
