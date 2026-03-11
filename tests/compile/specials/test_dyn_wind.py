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
Tests for koskari.compile compile/dyn-wind special form.

Dynamic-wind runs before/after thunks on extent entry and exit.
"""


from koskari.dis import dis
from . import SpecialsTestCase


class TestDynWindSpecial(SpecialsTestCase):

    async def test_before_and_after_run_in_order(self):
        """
        before runs on entry, body runs, after runs on exit
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/begin (compile/call record "body") 42)
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 42)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_after_only(self):
        """
        compile/dyn-wind with nil before and non-nil after
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          []
          (compile/begin (compile/call record "body") 1)
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 1)
        self.assertEqual(log, ["body", "after"])


    async def test_before_only(self):
        """
        compile/dyn-wind with non-nil before and nil after
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/begin (compile/call record "body") 2)
          [])
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 2)
        self.assertEqual(log, ["before", "body"])


    async def test_keyword_form_before_and_after(self):
        """
        compile/dyn-wind keyword form with before, body, and after
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (before: (compile/call record "before"))
          (body: (compile/call record "body") 42)
          (after: (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 42)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_keyword_form_after_only(self):
        """
        compile/dyn-wind keyword form with body and after only
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (body: (compile/call record "body") 1)
          (after: (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 1)
        self.assertEqual(log, ["body", "after"])


    async def test_keyword_form_before_only(self):
        """
        compile/dyn-wind keyword form with before and body only
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (before: (compile/call record "before"))
          (body: (compile/call record "body") 2))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 2)
        self.assertEqual(log, ["before", "body"])


    async def test_keyword_single_form_body(self):
        """
        Keyword form with single-form body (no extra list wrapper)
        """
        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (before: (compile/call record "before"))
          (body: (compile/begin (compile/call record "body") 77))
          (after: (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 77)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_positional_still_works(self):
        """
        Positional form continues to work after adding keyword form
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/begin (compile/call record "body") 99)
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 99)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_dyn_wind_body_value_is_expression_result(self):
        """
        dyn-wind expression evaluates to the body's final value
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/begin (compile/call record "body") 999)
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 999)
        self.assertEqual(log, ["before", "body", "after"])


    async def test_dyn_wind_return_from_nested_call_cleans_up_and_delivers_value(self):
        """
        When body calls a function that returns, after-thunk runs and value is delivered
        """

        log = []

        def record(s):
            log.append(s)

        src_str = """
        (compile/dyn-wind
          (compile/begin (compile/call record "before"))
          (compile/call (compile/lambda [] 99))
          (compile/begin (compile/call record "after")))
        """
        expr, mod = self.compile_expr(src_str, record=record)
        dis(expr.code)

        result = await expr()
        self.assertEqual(result, 99)
        self.assertEqual(log, ["before", "after"])


# The end.
