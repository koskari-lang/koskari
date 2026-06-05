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
Unit tests for tools.corpus.export

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

import pyarrow.parquet as pq


ROOT = Path(__file__).resolve().parent.parent.parent


def load_module(name, path):
    """
    Load a Python module from a file path

    :param name: module name for importlib
    :param path: path to the .py file
    :return: loaded module
    """

    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


export = load_module('corpus_export', ROOT / 'tools' / 'corpus' / 'export.py')
eval_view = load_module('corpus_view_eval', ROOT / 'tools' / 'corpus' / 'views' / 'eval.py')


FIXTURE_RECORDS = [
    {
        'id': 'fixture-eval-001',
        'feature': 'special/compile/cond',
        'source': '(cond [True 42])',
        'verify': {'eval': '42'},
    },
    {
        'id': 'fixture-read-only-001',
        'feature': 'layout/define-syntax',
        'source': 'define-syntax m:\n  m x:\n    x',
        'verify': {'read': '(define-syntax m ... )'},
    },
    {
        'id': 'fixture-compile-only-001',
        'feature': 'special/compile/lambda',
        'source': '(lambda [x] x)',
        'verify': {'compile': 'opcode:LAMBDA count:1'},
    },
]

SCHEMA_FIXTURE = {
    '$schema': 'https://json-schema.org/draft/2020-12/schema',
    '$id': 'https://example.test/corpus/schema.json',
    'title': 'fixture schema',
    'type': 'object',
}


class TestEvalView(unittest.TestCase):

    def test_transform_includes_eval_records(self):
        provenance = {
            'schema_version': 'https://example.test/corpus/schema.json',
            'git_ref': 'abc1234',
        }
        row = eval_view.transform(FIXTURE_RECORDS[0], provenance)
        self.assertEqual(row['corpus_id'], 'fixture-eval-001')
        self.assertEqual(row['eval'], '42')
        self.assertEqual(row['schema_version'], provenance['schema_version'])
        self.assertEqual(row['git_ref'], 'abc1234')

    def test_transform_skips_non_eval_records(self):
        provenance = {
            'schema_version': 'https://example.test/corpus/schema.json',
            'git_ref': '',
        }
        self.assertIsNone(eval_view.transform(FIXTURE_RECORDS[1], provenance))
        self.assertIsNone(eval_view.transform(FIXTURE_RECORDS[2], provenance))


class TestCorpusExport(unittest.TestCase):

    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.records_dir = self.root / 'records'
        self.records_dir.mkdir()
        self.schema_path = self.root / 'SCHEMA.json'
        self.schema_path.write_text(json.dumps(SCHEMA_FIXTURE))
        self.output_dir = self.root / 'out' / 'koskari-eval'

        shard = self.records_dir / 'fixture.jsonl'
        lines = [json.dumps(record) for record in FIXTURE_RECORDS]
        shard.write_text('\n'.join(lines) + '\n')

    def tearDown(self):
        self.tempdir.cleanup()

    def test_load_records_skips_blank_lines(self):
        shard = self.records_dir / 'blank.jsonl'
        shard.write_text('\n\n{"id": "x", "feature": "f", "source": "s", "verify": {"eval": "1"}}\n\n')
        records = export.load_records(self.records_dir)
        ids = [record['id'] for record in records]
        self.assertIn('x', ids)
        self.assertEqual(ids.count('x'), 1)

    def test_export_koskari_eval_row_count(self):
        path = export.export_corpus(
            'koskari-eval',
            self.records_dir,
            self.schema_path,
            self.output_dir,
            root=ROOT)
        self.assertTrue(path.exists())
        table = pq.read_table(path)
        self.assertEqual(table.num_rows, 1)

    def test_export_koskari_eval_columns(self):
        path = export.export_corpus(
            'koskari-eval',
            self.records_dir,
            self.schema_path,
            self.output_dir,
            root=ROOT)
        table = pq.read_table(path)
        row = table.to_pylist()[0]
        self.assertEqual(row['corpus_id'], 'fixture-eval-001')
        self.assertEqual(row['feature'], 'special/compile/cond')
        self.assertEqual(row['source'], '(cond [True 42])')
        self.assertEqual(row['eval'], '42')
        self.assertEqual(row['schema_version'], SCHEMA_FIXTURE['$id'])
        self.assertIn('git_ref', row)

    def test_malformed_json_reports_shard(self):
        bad = self.records_dir / 'bad.jsonl'
        bad.write_text('not json\n')
        with self.assertRaises(ValueError) as ctx:
            export.load_records(self.records_dir)
        self.assertIn('bad.jsonl:1:', str(ctx.exception))


# The end.
