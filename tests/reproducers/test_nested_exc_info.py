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
Handler-visible exception state across nested TRY and FINALLY regions.

A region that consumes a raise owns the exc_info it displaces and gives
it back when it leaves the chain, so a try nested in an except handler
or a finally body neither hides the outer exception from a bare raise
nor loses the exception a finally is propagating.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import sys

from tests.compile.specials import SpecialsTestCase

import pytest

pytestmark = pytest.mark.phase_reproducers


def _handled():
    exc = sys.exc_info()[1]
    return type(exc).__name__ if exc is not None else None


def _raise(exc):
    raise exc


class TestNestedExcInfo(SpecialsTestCase):

    async def _run(self, src, **values):
        expr, mod = self.compile_expr(
            src, handled=_handled, throw=_raise,
            ValueError=ValueError, KeyError=KeyError,
            RuntimeError=RuntimeError, Exception=Exception, str=str,
            **values)
        return await expr()


    async def test_try_inside_finally_keeps_the_propagating_exception(self):
        # the inner try catches its own KeyError; the outer ValueError
        # must still arrive at the outermost handler
        src = """
        (try
          (try
            (throw (ValueError "outer"))
            [finally: (try (throw (KeyError "inner")) (KeyError 0))])
          ([e ValueError] (str e)))
        """
        self.assertEqual("outer", await self._run(src))


    async def test_try_inside_finally_that_raises_nothing(self):
        src = """
        (try
          (try
            (throw (ValueError "outer"))
            [finally: (try 1 (KeyError 0))])
          ([e ValueError] (str e)))
        """
        self.assertEqual("outer", await self._run(src))


    async def test_finally_sees_the_outer_exception_around_the_inner_handler(self):
        # inside the finally body the propagating exception is handled;
        # a nested handler shows its own, and after it the outer is back
        src = """
        (begin
          (define seen (list))
          (try
            (try
              (throw (ValueError "outer"))
              [finally:
                (seen.append (handled))
                (try (throw (KeyError "inner")) (KeyError (seen.append (handled))))
                (seen.append (handled))])
            ([e ValueError] None))
          seen)
        """
        self.assertEqual(["ValueError", "KeyError", "ValueError"],
                         await self._run(src, list=list))


    async def test_bare_raise_after_nested_try_in_handler(self):
        src = """
        (try
          (try
            (throw (ValueError "outer"))
            ([e ValueError]
              (try 1 (KeyError 0))
              (raise)))
          ([e ValueError] (str e))
          ([e RuntimeError] "reraise lost"))
        """
        self.assertEqual("outer", await self._run(src))


    async def test_handled_exception_after_nested_try_in_handler(self):
        src = """
        (begin
          (define seen (list))
          (try
            (throw (ValueError "outer"))
            ([e ValueError]
              (seen.append (handled))
              (try (throw (KeyError "inner")) (KeyError (seen.append (handled))))
              (seen.append (handled))))
          seen)
        """
        self.assertEqual(["ValueError", "KeyError", "ValueError"],
                         await self._run(src, list=list))


    async def test_context_chains_from_the_outer_after_nested_try(self):
        src = """
        (try
          (try
            (throw (ValueError "outer"))
            ([e ValueError]
              (try 1 (KeyError 0))
              (throw (RuntimeError "second"))))
          ([e RuntimeError] e))
        """
        exc = await self._run(src)
        self.assertIsInstance(exc, RuntimeError)
        self.assertIsInstance(exc.__context__, ValueError)
        self.assertEqual(("outer",), exc.__context__.args)


    async def test_exception_state_clears_when_the_handler_ends(self):
        src = """
        (begin
          (define seen (list))
          (try (throw (ValueError "x")) ([e ValueError] (seen.append (handled))))
          (seen.append (handled))
          seen)
        """
        self.assertEqual(["ValueError", None], await self._run(src, list=list))


    async def test_return_from_a_handler_restores_the_caller_view(self):
        # the callee handles KeyError and returns from inside the handler;
        # the caller, itself inside a ValueError handler, still sees its own
        src = """
        (begin
          (define seen (list))
          (define f (lambda []
            (try (throw (KeyError "inner"))
              ([e KeyError] (seen.append (handled)) (return 1)))))
          (try
            (throw (ValueError "outer"))
            ([e ValueError]
              (seen.append (handled))
              (f)
              (seen.append (handled))))
          seen)
        """
        self.assertEqual(["ValueError", "KeyError", "ValueError"],
                         await self._run(src, list=list))


# The end.
