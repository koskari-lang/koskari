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
Corpus record verification tests.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


import pytest

from tests.corpus import CorpusTestCase, HANDLERS


pytestmark = pytest.mark.phase_corpus


def test_corpus_verify(record_id, stage, corpus_record):
    """
    Verify one corpus record at one pipeline stage
    """

    case = CorpusTestCase()
    case.setUp()
    try:
        case.bind_record(corpus_record)
        expected = corpus_record['verify'][stage]
        HANDLERS[stage](case, expected)
    finally:
        case.tearDown()


# The end.
