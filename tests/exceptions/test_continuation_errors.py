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
Tests for the KoskariContinuationError family.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from koskari.exceptions import (
    KoskariContinuationError, KoskariException, KoskariPromptNotFound,
    KoskariPromptUnreachable)


pytestmark = pytest.mark.phase_infra


class TestContinuationErrorHierarchy(unittest.TestCase):

    def test_category_roots_under_koskari_exception(self):
        self.assertTrue(
            issubclass(KoskariContinuationError, KoskariException))

    def test_leaves_root_under_category(self):
        self.assertTrue(
            issubclass(KoskariPromptNotFound, KoskariContinuationError))
        self.assertTrue(
            issubclass(KoskariPromptUnreachable, KoskariContinuationError))

    def test_leaves_are_distinct(self):
        self.assertFalse(
            issubclass(KoskariPromptNotFound, KoskariPromptUnreachable))
        self.assertFalse(
            issubclass(KoskariPromptUnreachable, KoskariPromptNotFound))

    def test_family_is_not_runtime_error(self):
        for cls in (KoskariContinuationError,
                    KoskariPromptNotFound,
                    KoskariPromptUnreachable):
            self.assertFalse(issubclass(cls, RuntimeError))


class TestContinuationErrorAttributes(unittest.TestCase):

    def test_base_attributes(self):
        exc = KoskariContinuationError("nope", 5, "shift")
        self.assertEqual(exc.message, "nope")
        self.assertEqual(exc.prompt_id, 5)
        self.assertEqual(exc.operation, "shift")
        self.assertEqual(str(exc), "nope")

    def test_base_attribute_defaults(self):
        exc = KoskariContinuationError("nope")
        self.assertIsNone(exc.prompt_id)
        self.assertIsNone(exc.operation)

    def test_unreachable_boundary(self):
        exc = KoskariPromptUnreachable("nope", 0, "shift", "generator")
        self.assertEqual(exc.prompt_id, 0)
        self.assertEqual(exc.operation, "shift")
        self.assertEqual(exc.boundary, "generator")

    def test_unreachable_boundary_default(self):
        exc = KoskariPromptUnreachable("nope")
        self.assertIsNone(exc.boundary)


# The end.
