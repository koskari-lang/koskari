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

import koskari  # noqa: E402,F401  (activates the .ki import hook)

from tools.corpus import load as corpus_load  # noqa: E402


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


class TestCorpusExport(unittest.TestCase):

    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.records_dir = self.root / 'records'
        self.records_dir.mkdir()
        self.schema_path = self.root / 'SCHEMA.json'
        self.schema_path.write_text(json.dumps(SCHEMA_FIXTURE))
        self.output_dir = self.root / 'out' / 'koskari-corpus'

        shard = self.records_dir / 'fixture.yaml'
        corpus_load.write_shard(shard, FIXTURE_RECORDS)

    def tearDown(self):
        self.tempdir.cleanup()

    def test_load_records_skips_blank_documents(self):
        shard = self.records_dir / 'blank.yaml'
        shard.write_text(
            '---\n'
            'id: x\n'
            'feature: f\n'
            'source: s\n'
            'verify:\n'
            '  eval: "1"\n'
            '\n'
            '---\n')
        records = export.load_records(self.records_dir)
        ids = [record['id'] for record in records]
        self.assertIn('x', ids)
        self.assertEqual(ids.count('x'), 1)

    def test_export_koskari_corpus_row_count(self):
        path = export.export_corpus(
            'koskari-corpus',
            self.records_dir,
            self.schema_path,
            self.output_dir,
            root=ROOT)
        self.assertTrue(path.exists())
        table = pq.read_table(path)
        self.assertEqual(table.num_rows, len(FIXTURE_RECORDS))

    def test_export_koskari_corpus_columns(self):
        path = export.export_corpus(
            'koskari-corpus',
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

    def test_malformed_yaml_reports_shard(self):
        bad = self.records_dir / 'bad.yaml'
        bad.write_text('id: [unclosed\n')
        with self.assertRaises(ValueError) as ctx:
            export.load_records(self.records_dir)
        self.assertIn('bad.yaml', str(ctx.exception))


class TestSourceViews(unittest.TestCase):

    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.schema_path = self.root / 'SCHEMA.json'
        self.schema_path.write_text(json.dumps(SCHEMA_FIXTURE))

    def tearDown(self):
        self.tempdir.cleanup()

    def test_export_source_view_docs(self):
        output_dir = self.root / 'out' / 'koskari-docs'
        path = export.export_source_view(
            'koskari-docs',
            self.schema_path,
            output_dir,
            root=ROOT)
        self.assertTrue(path.exists())
        table = pq.read_table(path)
        self.assertGreater(table.num_rows, 0)
        row = table.to_pylist()[0]
        self.assertEqual(row['kind'], 'doc')
        self.assertIn('content', row)
        self.assertEqual(row['schema_version'], SCHEMA_FIXTURE['$id'])

    def test_export_all_views_includes_source_views(self):
        records_dir = self.root / 'records'
        records_dir.mkdir()
        corpus_load.write_shard(
            records_dir / 'fixture.yaml', FIXTURE_RECORDS)
        output_base = self.root / 'datasets'

        paths = export.export_all_views(
            records_dir,
            self.schema_path,
            output_base,
            root=ROOT)

        names = {path.parent.name for path in paths}
        self.assertEqual(
            names,
            {'koskari-corpus',
             'koskari-docs', 'koskari-programs'})
        self.assertTrue(
            (output_base / 'koskari-docs' / 'train.parquet').exists())
        self.assertTrue(
            (output_base / 'koskari-programs' / 'train.parquet').exists())


# The end.
