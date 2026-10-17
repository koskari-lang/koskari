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
tests.matcher.test_oracle

The matcher oracle on the matcher core: every case of the corpus,
standalone patterns and inline match arms, gives master's outcome or
differs from it only as one of the deliberate changes the comparison's
rules name

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""

import unittest

import pytest

from tests.matcher.oracle import compare, format_report


pytestmark = pytest.mark.phase_infra


class TestOracle(unittest.TestCase):

    def test_core_agrees_with_master(self):
        report = compare(selector=True)
        self.assertFalse(
            report["unexplained"], "\n" + format_report(report, limit=5))


# The end.
