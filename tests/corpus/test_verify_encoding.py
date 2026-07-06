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
Unit tests for corpus verify.eval encodings and marked complex checks.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from koskari.lib import KosType

from tests.corpus import (
    CorpusTestCase,
    eval_values_equal,
    parse_eval_expected,
    run_marked_eval_check,
)


pytestmark = pytest.mark.phase_corpus


class TestParseEvalExpected(unittest.TestCase):

    def test_literal_scalar(self):
        kind, payload = parse_eval_expected('42')
        self.assertEqual(kind, 'literal')
        self.assertEqual(payload, 42)


    def test_marked_kostype(self):
        kind, payload = parse_eval_expected('@kostype:Counter')
        self.assertEqual(kind, 'marked')
        self.assertEqual(payload, 'kostype:Counter')


    def test_literal_not_confused_with_marked(self):
        kind, payload = parse_eval_expected('"5"')
        self.assertEqual(kind, 'literal')
        self.assertEqual(payload, '5')


class TestMarkedEvalCheck(unittest.TestCase):

    def test_kostype_check_passes(self):
        class Counter(metaclass=KosType):
            pass

        Counter.__name__ = 'Counter'
        case = unittest.TestCase()
        run_marked_eval_check(case, Counter, 'kostype:Counter')


    def test_kostype_check_fails_on_wrong_name(self):
        class Counter(metaclass=KosType):
            pass

        Counter.__name__ = 'Counter'
        case = unittest.TestCase()
        with self.assertRaises(AssertionError):
            run_marked_eval_check(case, Counter, 'kostype:Other')


    def test_unknown_marker_raises(self):
        case = unittest.TestCase()
        with self.assertRaises(ValueError) as ctx:
            run_marked_eval_check(case, 42, 'unknown:thing')
        self.assertIn('unknown', str(ctx.exception))


    def test_forbidden_opcode_marker_raises(self):
        case = unittest.TestCase()
        with self.assertRaises(ValueError) as ctx:
            run_marked_eval_check(case, 42, 'opcode:FOO')
        self.assertIn('bytecode', str(ctx.exception))


class TestCorpusMarkedEvalIntegration(unittest.TestCase):

    def test_lambda_return_annotation_record(self):
        case = CorpusTestCase()
        case.setUp()
        try:
            case.bind_record({
                'id': 'fixture-annotations-001',
                'feature': 'special/compile/lambda',
                'source': '(lambda :: int [x] x)',
                'verify': {'eval': '@annotations:return:int'},
            })
            got = case.eval_source(case.record['source'])
            run_marked_eval_check(case, got, 'annotations:return:int')
        finally:
            case.tearDown()


    def test_literal_eval_still_works(self):
        self.assertTrue(eval_values_equal(5, 5))
        kind, payload = parse_eval_expected('5')
        self.assertEqual(kind, 'literal')
        self.assertEqual(payload, 5)


# The end.
