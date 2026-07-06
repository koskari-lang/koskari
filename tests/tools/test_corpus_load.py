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
Unit tests for tools.corpus.load

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import importlib.util
import tempfile
import unittest
from pathlib import Path


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


corpus_load = load_module('corpus_load', ROOT / 'tools' / 'corpus' / 'load.py')


class TestCorpusLoad(unittest.TestCase):

    def test_multiline_source_round_trip(self):
        """
        Block-scalar source preserves newlines without a spurious trailing one
        """

        record = {
            'id': 'fixture-layout-001',
            'feature': 'layout/if',
            'source': 'if True:\n  1\nelse:\n  2',
            'verify': {
                'read': '(compile/cond (True 1) (else: 2))',
                'eval': '1',
            },
        }
        text = corpus_load.dump_record(record)
        loaded = list(__import__('yaml').safe_load_all(text))[0]
        self.assertEqual(loaded['source'], record['source'])


    def test_null_eval_stays_string(self):
        """
        verify.eval null encodes as a quoted YAML string, not YAML null
        """

        record = {
            'id': 'fixture-null-eval-001',
            'feature': 'special/compile/cond',
            'source': '(cond)',
            'verify': {'eval': 'null'},
        }
        text = corpus_load.dump_record(record)
        self.assertIn('eval: "null"', text)
        loaded = list(__import__('yaml').safe_load_all(text))[0]
        self.assertEqual(loaded['verify']['eval'], 'null')
        self.assertIsInstance(loaded['verify']['eval'], str)


    def test_load_records_skips_blank_documents(self):
        """
        Empty YAML documents between separators are ignored
        """

        tempdir = tempfile.TemporaryDirectory()
        try:
            records_dir = Path(tempdir.name)
            shard = records_dir / 'blank.yaml'
            shard.write_text(
                '---\n'
                'id: x\n'
                'feature: f\n'
                'source: s\n'
                'verify:\n'
                '  eval: "1"\n'
                '\n'
                '---\n'
                '\n'
                '---\n'
                'id: y\n'
                'feature: f\n'
                'source: s\n'
                'verify:\n'
                '  eval: "2"\n')
            records = corpus_load.load_records(records_dir)
            ids = [record['id'] for record in records]
            self.assertEqual(ids, ['x', 'y'])
        finally:
            tempdir.cleanup()


    def test_malformed_yaml_reports_shard(self):
        """
        YAML parse errors include the shard filename
        """

        tempdir = tempfile.TemporaryDirectory()
        try:
            records_dir = Path(tempdir.name)
            bad = records_dir / 'bad.yaml'
            bad.write_text('id: [unclosed\n')
            with self.assertRaises(ValueError) as ctx:
                corpus_load.load_records(records_dir)
            self.assertIn('bad.yaml', str(ctx.exception))
        finally:
            tempdir.cleanup()


    def test_hash_source_is_quoted(self):
        """
        Source containing # is double-quoted so YAML does not treat it as a comment
        """

        record = {
            'id': 'fixture-hash-001',
            'feature': 'special/compile/item',
            'source': '(begin (define lst #[10 20 30]) (item lst 0))',
            'verify': {'eval': '10'},
        }
        text = corpus_load.dump_record(record)
        self.assertIn('#', text)
        self.assertTrue('source: "' in text or "source: '" in text)
        loaded = list(__import__('yaml').safe_load_all(text))[0]
        self.assertEqual(loaded['source'], record['source'])


    def test_quoted_string_eval_round_trip(self):
        """
        verify.eval JSON string literals keep their surrounding quote characters
        """

        record = {
            'id': 'fixture-repr-001',
            'feature': 'special/compile/repr',
            'source': '(repr 42)',
            'verify': {'eval': '"42"'},
        }
        text = corpus_load.dump_record(record)
        loaded = list(__import__('yaml').safe_load_all(text))[0]
        self.assertEqual(loaded['verify']['eval'], '"42"')


    def test_dump_shard_round_trip(self):
        """
        dump_shard and load_records preserve record content
        """

        tempdir = tempfile.TemporaryDirectory()
        try:
            records_dir = Path(tempdir.name)
            shard = records_dir / 'fixture.yaml'
            records = [
                {
                    'id': 'fixture-a',
                    'feature': 'special/compile/cond',
                    'source': '(cond)',
                    'verify': {'eval': 'null'},
                    'tags': ['?', 'surface:block'],
                },
                {
                    'id': 'fixture-b',
                    'feature': 'layout/if',
                    'source': 'if True:\n  1',
                    'verify': {'read': '(compile/cond (True 1))'},
                },
            ]
            corpus_load.write_shard(shard, records)
            loaded = corpus_load.load_records(records_dir)
            self.assertEqual(len(loaded), 2)
            self.assertEqual(loaded[0]['id'], 'fixture-a')
            self.assertEqual(loaded[0]['tags'], ['?', 'surface:block'])
            self.assertEqual(loaded[1]['source'], 'if True:\n  1')
        finally:
            tempdir.cleanup()


# The end.
