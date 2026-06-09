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
Pytest fixtures and parametrization for corpus tests.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


import pytest

from tests.corpus import load_corpus_cases, load_corpus_index


@pytest.fixture(scope='session')
def corpus_index():
    """
    Session-scoped corpus record index keyed by id
    """

    return load_corpus_index()


@pytest.fixture
def corpus_record(corpus_index, record_id):
    """
    Corpus record for the current parametrized test
    """

    return corpus_index[record_id]


def pytest_generate_tests(metafunc):
    """
    Parametrize corpus tests over record id and verify stage
    """

    if 'record_id' not in metafunc.fixturenames:
        return
    if 'stage' not in metafunc.fixturenames:
        return

    cases = load_corpus_cases()
    ids = ['%s/%s' % (record_id, stage) for record_id, stage in cases]
    metafunc.parametrize('record_id,stage', cases, ids=ids)


# The end.
