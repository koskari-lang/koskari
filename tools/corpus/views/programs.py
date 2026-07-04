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
tools.corpus.views.programs

koskari-programs dataset view: example programs served to agents.

One row per Koskari source file and README under examples/, giving agents
composition-level material (whole programs) rather than single-construct
snippets. Unlike the corpus, these rows are ILLUSTRATIVE and NOT verified;
the dataset card labels the config accordingly.

The same whole-file/Viewer-truncation caveat documented in docs.py applies
to the ``content`` column here.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.8 via Claude Code
"""


import importlib.util
from pathlib import Path


VIEW_NAME = 'koskari-programs'

_INCLUDE_SUFFIXES = ('.ki',)
_INCLUDE_NAMES = ('README.md',)


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


def _is_build_artifact(rel_parts):
    """
    Return True when a path passes through a build output directory

    :param rel_parts: path components relative to the repository root
    :return: True when any component is a build directory
    """

    return 'build' in rel_parts


def _section(rel_parts):
    """
    Return the example group for a repo-relative path

    :param rel_parts: path components relative to the repository root
    :return: the top-level example group (e.g. examples/life)
    """

    if len(rel_parts) > 2:
        return '/'.join(rel_parts[:2])
    return rel_parts[0]


def _included(path):
    """
    Return True when a file should be published as a program row

    :param path: candidate file path
    :return: True for Koskari sources and example READMEs
    """

    return path.suffix in _INCLUDE_SUFFIXES or path.name in _INCLUDE_NAMES


def generate(root, provenance):
    """
    Build koskari-programs rows from files under examples/

    :param root: repository root path
    :param provenance: dict with schema_version and git_ref keys
    :return: list of row dicts sorted by path
    """

    material = _load_material()
    root = Path(root)
    examples_dir = root / 'examples'

    rows = []
    if not examples_dir.is_dir():
        return rows

    for path in examples_dir.rglob('*'):
        if not path.is_file() or not _included(path):
            continue
        rel_parts = path.relative_to(root).parts
        if _is_build_artifact(rel_parts):
            continue

        text = path.read_text(encoding='utf-8')
        rel_path = path.relative_to(root).as_posix()
        if path.suffix == '.ki':
            title, summary = material.ki_title_summary(text, path.stem)
        else:
            text = material.rewrite_markdown_links(text, rel_path)
            title, summary = material.markdown_title_summary(text)
            title = title or path.stem

        rows.append({
            'path': rel_path,
            'title': title,
            'summary': summary,
            'content': text,
            'section': _section(rel_parts),
            'kind': 'example',
            'schema_version': provenance['schema_version'],
            'git_ref': provenance['git_ref'],
        })

    rows.sort(key=lambda row: row['path'])
    return rows


# The end.
