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
Full-stack test for the baked, in-memory corpus MCP backend.

Stages synthetic parquet views the way ``make mcp-data`` would, then drives
every tool in ``tools/mcp/mcp_server.ki`` through the koskari host and pins
the observable contract: token ranking, the substring fallback, feature and
polarity filtering, wrong-usage demotion, reference resolution, doc/program
routing, and miss messages. Deterministic and offline -- no network and no
dependency on the real corpus snapshot.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import asyncio
import io
import json
import os
import sys
import unittest

import pytest


pytestmark = pytest.mark.phase_reproducers

# mcp 2.x declares Requires-Python >=3.10, so the dependency is absent
# from the 3.9 test env by design (see the marker in setup.cfg).
if sys.version_info < (3, 10):
    pytest.skip(
        "tools/mcp requires mcp 2.x, which needs Python 3.10 or newer",
        allow_module_level=True)

pyarrow = pytest.importorskip("pyarrow")
pytest.importorskip("mcp")
import pyarrow.parquet as pq  # noqa: E402


_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_MCP_DIR = os.path.join(_REPO_ROOT, 'tools', 'mcp')
_SCRIPT = os.path.join(os.path.dirname(__file__), 'scripts', 'mcp_backend.ki')


# Full key set for the koskari-corpus view; every row carries every key so
# pyarrow infers a stable schema.
_CORPUS_KEYS = (
    'corpus_id', 'feature', 'source', 'references', 'stability', 'tags',
    'schema_version', 'git_ref', 'read', 'expand', 'eval', 'error',
    'invalid')


def _corpus_row(corpus_id, feature, source, tags, references):
    row = dict.fromkeys(_CORPUS_KEYS)
    row.update({
        'corpus_id': corpus_id,
        'feature': feature,
        'source': source,
        'references': references,
        'stability': 'evolving',
        'tags': tags,
        'schema_version': 'test-schema',
        'git_ref': 'testref',
        'eval': 'ok',
    })
    return row


def _doc_row(path, title, summary, content, kind):
    return {
        'path': path,
        'title': title,
        'summary': summary,
        'content': content,
        'section': path.split('/')[0],
        'kind': kind,
        'schema_version': 'test-schema',
        'git_ref': 'testref',
    }


_CORPUS_ROWS = [
    _corpus_row(
        'ex-while', 'loops/while', '(while cond (step))', [],
        ['guide/intro.md']),
    _corpus_row(
        'ex-foreach', 'loops/for-each', '(for-each [x xs] (use x))',
        ['loop', 'iterate'], []),
    _corpus_row(
        'ex-while-bad', 'loops/while', '(while #t (step))', ['invalid'],
        []),
    # ahead of ex-map so a feature fallback over functions/ would hand
    # back this row first; only ex-map holds the token xs
    _corpus_row(
        'ex-reduce', 'functions/reduce', '(reduce fn acc)', [], []),
    _corpus_row(
        'ex-map', 'functions/map', '(map fn xs)', ['higher-order'], []),
]

_DOCS_ROWS = [
    _doc_row(
        'guide/intro.md', 'Intro', 'the intro',
        'An introduction to loops and while.', 'doc'),
]

_PROGRAMS_ROWS = [
    _doc_row(
        'examples/hello.ki', 'Hello', 'hello program',
        '(print "greetings")', 'example'),
]


def _write_view(base, view, rows):
    view_dir = os.path.join(base, view)
    os.makedirs(view_dir, exist_ok=True)
    pq.write_table(
        pyarrow.Table.from_pylist(rows),
        os.path.join(view_dir, 'train.parquet'))


def _stage_dataset(base):
    _write_view(base, 'koskari-corpus', _CORPUS_ROWS)
    _write_view(base, 'koskari-docs', _DOCS_ROWS)
    _write_view(base, 'koskari-programs', _PROGRAMS_ROWS)


class TestMcpBakedBackend(unittest.IsolatedAsyncioTestCase):
    """
    The MCP tools serve correct results from baked parquet, offline.
    """

    async def asyncSetUp(self):
        import tempfile

        from koskari.cli.host import run_file_async
        from koskari.importer import install
        from koskari.module import new_module

        self._run_file_async = run_file_async
        self._new_module = new_module

        self._tmp = tempfile.mkdtemp(prefix='mcp-backend-')
        data_dir = os.path.join(self._tmp, 'data')
        _stage_dataset(data_dir)

        self._prev_data = os.environ.get('KOSKARI_CORPUS_DATA')
        os.environ['KOSKARI_CORPUS_DATA'] = data_dir

        self._path_added = _MCP_DIR not in sys.path
        if self._path_added:
            sys.path.insert(0, _MCP_DIR)

        install()

    async def asyncTearDown(self):
        import shutil
        if self._prev_data is None:
            os.environ.pop('KOSKARI_CORPUS_DATA', None)
        else:
            os.environ['KOSKARI_CORPUS_DATA'] = self._prev_data
        if self._path_added and _MCP_DIR in sys.path:
            sys.path.remove(_MCP_DIR)
        shutil.rmtree(self._tmp, ignore_errors=True)

    async def _run(self):
        mod = self._new_module('__main__')
        captured = io.StringIO()
        orig = sys.stdout
        sys.stdout = captured
        try:
            await self._run_file_async(mod, _SCRIPT)
        finally:
            sys.stdout = orig
        emitted = {}
        for line in captured.getvalue().splitlines():
            if line.startswith('[[') and ']] ' in line:
                tag, _, payload = line[2:].partition(']] ')
                emitted[tag] = json.loads(payload)
        return emitted

    async def test_tool_contract(self):
        out = await self._run()

        # provenance surface
        self.assertEqual(out['version']['git_ref'], 'testref')
        self.assertEqual(out['version']['corpus'], 5)
        self.assertEqual(out['version']['docs'], 1)
        self.assertEqual(out['version']['programs'], 1)

        # token ranking: exactly the one row whose column holds the token
        self.assertEqual(out['map'], ['ex-map'])

        # wrong-usage record is retained but demoted below the valid one
        self.assertEqual(out['while'], ['ex-while', 'ex-while-bad'])
        self.assertEqual(out['while_valid'], ['ex-while'])
        self.assertEqual(out['while_invalid'], ['ex-while-bad'])

        # substring fallback resolves a fragment no whole token matches
        self.assertEqual(out['each_fallback'], ['ex-foreach'])

        # a feature selector stands on its own; invalid still sorts last
        self.assertEqual(
            out['feature_loops'], ['ex-while', 'ex-foreach', 'ex-while-bad'])
        self.assertEqual(out['feature_loops'][-1], 'ex-while-bad')

        # the feature narrows the search before the limit cuts it: the
        # one functions/ row holding xs wins even at limit 1, although it
        # ranks below a loops/ row corpus-wide
        self.assertEqual(out['xs_functions'], ['ex-map'])

        # references resolve to their doc title/summary
        resolved = out['get_refs']
        self.assertEqual(len(resolved), 1)
        self.assertEqual(resolved[0]['path'], 'guide/intro.md')
        self.assertEqual(resolved[0]['title'], 'Intro')

        # the served icon is sourced from koskari.assets, not a local copy
        self.assertTrue(out['icon_matches_packaged_asset'])

        # doc vs program routing
        self.assertEqual(out['docs_loops'], ['guide/intro.md'])
        self.assertEqual(out['prog_greet'], ['examples/hello.ki'])
        self.assertEqual(out['ref_prog']['path'], 'examples/hello.ki')
        self.assertEqual(out['ref_prog']['kind'], 'example')
        self.assertIn('greetings', out['ref_prog']['content'])

        # overview tools
        self.assertIn('loops/', out['features'])
        self.assertIn('functions/', out['features'])
        self.assertIn('iterate', out['tags'])
        self.assertIn('invalid', out['tags'])

        # misses report clearly rather than raising
        self.assertIn('No corpus example', out['miss_id'])
        self.assertIn('No matching corpus examples', out['miss_search'])


# The end.
