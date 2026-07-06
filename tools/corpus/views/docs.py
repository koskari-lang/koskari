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
tools.corpus.views.docs

koskari-docs dataset view: user documentation pages served to agents.

One row per Markdown page under the user-facing docs subtrees named in
``_DOC_SUBDIRS`` (reference, start, guide, patterns). The row
``path`` is the docs-relative path, which matches the strings stored in
corpus records' ``references`` field verbatim, so the MCP can resolve a
record's references to their documentation content.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


# =====================================================================
# WHOLE-FILE CONTENT CELLS AND HUGGING FACE VIEWER TRUNCATION
# ---------------------------------------------------------------------
# Each documentation page is stored as ONE row with the entire file text
# in the ``content`` column. Downstream, the MCP server reads these rows
# through the Hugging Face Dataset Viewer API, which imposes a per-cell
# (and per-response) size cap: a sufficiently large page would be
# TRUNCATED in the API response, so `get_reference` would return partial
# content with no error.
#
# Today this is safe: reference pages average a few kilobytes, well under
# the cap. We have DELIBERATELY DEFERRED any chunking. If pages grow (or
# a single generated page becomes large), the mitigation is to split each
# document into multiple rows keyed by heading (e.g. path + anchor) so no
# one cell approaches the Viewer limit. That change touches this view,
# the dataset card's `docs` config columns, and the MCP `get_reference`
# reassembly logic. Revisit when the largest page approaches the cap.
# =====================================================================


import importlib.util
from pathlib import Path


VIEW_NAME = 'koskari-docs'

_DOC_SUBDIRS = ('reference', 'start', 'guide', 'patterns')


def _load_material():
    """
    Load the shared material helpers module

    :return: _material module
    """

    path = Path(__file__).resolve().parent / '_material.py'
    spec = importlib.util.spec_from_file_location('corpus_material', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _section(rel_path):
    """
    Return the parent directory of a docs-relative path as its section

    :param rel_path: docs-relative posix path
    :return: parent directory string, or the top-level name
    """

    parent = rel_path.rsplit('/', 1)[0] if '/' in rel_path else rel_path
    return parent


def generate(root, provenance):
    """
    Build koskari-docs rows from Markdown pages under docs/

    :param root: repository root path
    :param provenance: dict with schema_version and git_ref keys
    :return: list of row dicts sorted by path
    """

    material = _load_material()
    docs_dir = Path(root) / 'docs'

    rows = []
    for subdir in _DOC_SUBDIRS:
        base = docs_dir / subdir
        if not base.is_dir():
            continue
        for path in base.rglob('*.md'):
            rel_path = path.relative_to(docs_dir).as_posix()
            text = material.rewrite_markdown_links(
                path.read_text(encoding='utf-8'), rel_path)
            title, summary = material.markdown_title_summary(text)
            rows.append({
                'path': rel_path,
                'title': title or path.stem,
                'summary': summary,
                'content': text,
                'section': _section(rel_path),
                'kind': 'doc',
                'schema_version': provenance['schema_version'],
                'git_ref': provenance['git_ref'],
            })

    rows.sort(key=lambda row: row['path'])
    return rows


# The end.
