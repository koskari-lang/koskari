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
Unit tests for corpus dataset view transforms.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


import importlib.util
from pathlib import Path

import pytest

from tests.corpus import default_records_dir, load_records


pytestmark = pytest.mark.phase_corpus

_FULL_COLUMNS = frozenset({
    'corpus_id',
    'feature',
    'source',
    'read',
    'expand',
    'eval',
    'error',
    'invalid',
    'references',
    'stability',
    'tags',
    'schema_version',
    'git_ref',
})

_EVAL_COLUMNS = frozenset({
    'corpus_id',
    'feature',
    'source',
    'eval',
    'schema_version',
    'git_ref',
})


def _load_view_module(filename):
    """
    Load a corpus view module from tools/corpus/views/
    """

    path = Path(__file__).resolve().parents[2] / 'tools' / 'corpus' / 'views'
    path = path / filename
    name = 'test_corpus_view_' + path.stem
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _apply_view(transform, records):
    """
    Apply a view transform to every record
    """

    provenance = {
        'schema_version': 'https://koskari.dev/corpus/schema.json',
        'git_ref': '',
    }
    rows = []
    for record in records:
        row = transform(record, provenance)
        if row is not None:
            rows.append(row)
    return rows


def test_full_view_includes_every_record():
    """
    koskari-corpus emits one row per loaded record
    """

    full = _load_view_module('full.py')
    records = load_records(default_records_dir())
    rows = _apply_view(full.transform, records)
    assert len(rows) == len(records)


def test_full_view_columns():
    """
    koskari-corpus rows expose all expected columns
    """

    full = _load_view_module('full.py')
    records = load_records(default_records_dir())
    rows = _apply_view(full.transform, records)
    assert rows
    for row in rows:
        assert set(row.keys()) == _FULL_COLUMNS


def test_eval_view_omits_records_without_eval():
    """
    koskari-eval drops records lacking verify.eval
    """

    eval_mod = _load_view_module('eval.py')
    records = load_records(default_records_dir())
    rows = _apply_view(eval_mod.transform, records)
    eval_count = sum(
        1 for record in records
        if (record.get('verify') or {}).get('eval') is not None)
    assert len(rows) == eval_count
    assert len(rows) < len(records)


def test_eval_view_columns():
    """
    koskari-eval rows expose only eval-stage columns
    """

    eval_mod = _load_view_module('eval.py')
    records = load_records(default_records_dir())
    rows = _apply_view(eval_mod.transform, records)
    assert rows
    for row in rows:
        assert set(row.keys()) == _EVAL_COLUMNS


# The end.
