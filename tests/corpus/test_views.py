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
"""


import importlib

import pytest

from tests.corpus import default_records_dir, load_records, project_root


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

_MATERIAL_COLUMNS = frozenset({
    'path',
    'title',
    'summary',
    'content',
    'section',
    'kind',
    'schema_version',
    'git_ref',
})


def _load_view_module(name):
    """
    Load a corpus view module from tools/corpus/views/

    The view modules are written in Koskari, so this resolves them
    through the koskari import hook rather than by filesystem path.

    :param name: bare module name, e.g. ``full`` or ``_material``
    :return: the imported view module
    """

    import koskari  # noqa: F401  (activates the .ki import hook)

    return importlib.import_module('tools.corpus.views.%s' % name)


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


def _generate_source_view(mod):
    """
    Run a source-generated view over the repository root
    """

    provenance = {
        'schema_version': 'https://koskari.dev/corpus/schema.json',
        'git_ref': '',
    }
    return mod.generate(project_root(), provenance)


def test_full_view_includes_every_record():
    """
    koskari-corpus emits one row per loaded record
    """

    full = _load_view_module('full')
    records = load_records(default_records_dir())
    rows = _apply_view(full.transform, records)
    assert len(rows) == len(records)


def test_full_view_columns():
    """
    koskari-corpus rows expose all expected columns
    """

    full = _load_view_module('full')
    records = load_records(default_records_dir())
    rows = _apply_view(full.transform, records)
    assert rows
    for row in rows:
        assert set(row.keys()) == _FULL_COLUMNS


def test_docs_view_row_per_markdown_page():
    """
    koskari-docs emits one row per Markdown page under each published
    docs subtree
    """

    docs = _load_view_module('docs')
    rows = _generate_source_view(docs)

    root = project_root()
    expected = 0
    for subdir in docs._DOC_SUBDIRS:
        expected += len(list((root / 'docs' / subdir).rglob('*.md')))

    assert rows
    assert len(rows) == expected


def test_docs_view_columns():
    """
    koskari-docs rows expose the material column set and doc kind
    """

    docs = _load_view_module('docs')
    rows = _generate_source_view(docs)
    assert rows
    for row in rows:
        assert set(row.keys()) == _MATERIAL_COLUMNS
        assert row['kind'] == 'doc'
        assert row['content']


def test_docs_view_resolves_all_record_references():
    """
    Every documentation reference cited by a corpus record resolves to a
    koskari-docs row, so the MCP can turn a record's references into
    documentation content. Guards against reintroducing dangling references.
    """

    docs = _load_view_module('docs')
    doc_paths = {row['path'] for row in _generate_source_view(docs)}

    records = load_records(default_records_dir())
    published = tuple(sub + '/' for sub in docs._DOC_SUBDIRS)
    cited = set()
    for record in records:
        for ref in record.get('references') or []:
            if ref.endswith('.md') and ref.startswith(published):
                cited.add(ref)

    assert cited
    missing = cited - doc_paths
    assert not missing, 'dangling doc references: %s' % sorted(missing)


def test_programs_view_columns():
    """
    koskari-programs rows expose the material column set and example kind
    """

    programs = _load_view_module('programs')
    rows = _generate_source_view(programs)
    assert rows
    for row in rows:
        assert set(row.keys()) == _MATERIAL_COLUMNS
        assert row['kind'] == 'example'
        assert row['path'].startswith('examples/')


def test_programs_view_excludes_build_artifacts():
    """
    koskari-programs skips generated build/ trees under examples/
    """

    programs = _load_view_module('programs')
    rows = _generate_source_view(programs)
    assert rows
    for row in rows:
        assert '/build/' not in row['path']


def test_programs_view_extracts_summaries():
    """
    koskari-programs derives summaries from docstrings and comment banners
    """

    programs = _load_view_module('programs')
    rows = {row['path']: row for row in _generate_source_view(programs)}

    life = rows.get('examples/life/koslife/life.ki')
    assert life is not None
    assert life['summary'] and 'Life' in life['summary']

    dataclass = rows.get('examples/dataclass.ki')
    assert dataclass is not None
    assert dataclass['summary'] and 'dataclass' in dataclass['summary']


def test_rewrite_markdown_links_roots_relative_targets():
    """
    Relative Markdown links resolve to root-relative content paths that
    match the row ``path`` scheme, so get_reference can follow them
    """

    material = _load_view_module('_material')
    rewrite = material.rewrite_markdown_links
    page = 'start/from-python/map.md'

    assert rewrite('[a](layout-and-calls.md)', page) == (
        '[a](start/from-python/layout-and-calls.md)')
    assert rewrite('[a](../../reference/specials/index.md)', page) == (
        '[a](reference/specials/index.md)')
    # a link that climbs out of docs/ into the sibling examples/ tree
    # normalizes to the repo-relative scheme the programs view publishes
    assert rewrite('[a](../../examples/hello.ki)', page) == (
        '[a](examples/hello.ki)')


def test_rewrite_markdown_links_preserves_anchor_and_leaves_externals():
    """
    Fragments ride along; external, mail, absolute, and pure-anchor
    links are left untouched
    """

    material = _load_view_module('_material')
    rewrite = material.rewrite_markdown_links
    page = 'reference/layouts/layout-if.md'

    assert rewrite('[a](../reader-layout.md#peers)', page) == (
        '[a](reference/reader-layout.md#peers)')
    for untouched in (
            '[a](https://example.com/x.md)',
            '[a](mailto:x@example.com)',
            '[a](#local-anchor)',
            '[a](/site/absolute.md)'):
        assert rewrite(untouched, page) == untouched


def test_rewrite_markdown_links_skips_fenced_code():
    """
    Link-like text inside fenced code blocks is left verbatim
    """

    material = _load_view_module('_material')
    text = '```\n[a](../other.md)\n```\n[b](../other.md)\n'
    result = material.rewrite_markdown_links(text, 'start/from-python/map.md')

    assert '[a](../other.md)' in result
    assert '[b](start/other.md)' in result


def test_rewrite_toctree_normalizes_non_glob_entries():
    """
    Non-glob {toctree} child entries become root-relative .md paths, and
    directive option lines are left untouched
    """

    material = _load_view_module('_material')
    text = (
        '# Reference\n\n'
        '```{toctree}\n'
        ':hidden:\n'
        ':maxdepth: 1\n'
        ':caption: Overview\n'
        '\n'
        'glossary\n'
        'concepts/index\n'
        'specials/index\n'
        '```\n')
    result = material.rewrite_markdown_links(text, 'reference/index.md')

    assert 'reference/glossary.md' in result
    assert 'reference/concepts/index.md' in result
    assert 'reference/specials/index.md' in result
    assert ':caption: Overview' in result
    assert ':maxdepth: 1' in result


def test_rewrite_toctree_leaves_glob_blocks_unchanged():
    """
    A {toctree} carrying :glob: names patterns, not pages, so its body is
    left verbatim
    """

    material = _load_view_module('_material')
    text = (
        '# Specials\n\n'
        '```{toctree}\n'
        ':maxdepth: 1\n'
        ':glob:\n'
        '\n'
        '*\n'
        '   compile-*\n'
        '```\n')
    result = material.rewrite_markdown_links(
        text, 'reference/specials/index.md')

    assert result == text


def test_rewrite_toctree_handles_titled_entry():
    """
    The ``Title <docname>`` toctree form rewrites only the docname
    """

    material = _load_view_module('_material')
    text = '```{toctree}\nThe Map <map>\n```\n'
    result = material.rewrite_markdown_links(text, 'start/from-python/index.md')

    assert 'The Map <start/from-python/map.md>' in result


def test_docs_view_normalizes_toctree_navigation():
    """
    Real index pages expose their toctree children as fetchable
    root-relative paths, while glob indexes keep their patterns
    """

    docs = _load_view_module('docs')
    rows = {row['path']: row for row in _generate_source_view(docs)}

    from_python = rows['start/from-python/index.md']['content']
    assert 'start/from-python/map.md' in from_python
    assert 'start/from-python/layout-and-calls.md' in from_python

    reference = rows['reference/index.md']['content']
    assert 'reference/concepts/index.md' in reference
    assert 'reference/specials/index.md' in reference

    specials = rows['reference/specials/index.md']['content']
    assert 'compile-*' in specials
    assert 'compile-*.md' not in specials


# The end.
