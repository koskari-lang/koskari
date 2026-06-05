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
tools.corpus.export

Export corpus JSONL records to parquet dataset views.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


import argparse
import importlib.util
import json
import sys
from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq


def project_root(root=None):
    """
    Return the repository root path

    :param root: optional override; defaults to parent of tools/
    :return: Path to repository root
    """

    if root is not None:
        return Path(root).resolve()
    return Path(__file__).resolve().parent.parent.parent


def load_regen_version():
    """
    Load tools.regen_version as a module

    :return: regen_version module
    """

    path = project_root() / 'tools' / 'regen_version.py'
    spec = importlib.util.spec_from_file_location('regen_version', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load_views():
    """
    Load tools.corpus.views as a module

    :return: views module with VIEWS registry
    """

    path = Path(__file__).resolve().parent / 'views' / '__init__.py'
    spec = importlib.util.spec_from_file_location('corpus_views', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def read_schema_version(schema_path):
    """
    Read the corpus schema identifier from SCHEMA.json

    :param schema_path: path to SCHEMA.json
    :return: schema $id string
    """

    data = json.loads(Path(schema_path).read_text())
    return data.get('$id', '')


def git_ref(root=None):
    """
    Return short git commit for provenance, or empty string

    :param root: repository root path
    :return: short commit hash or empty string
    """

    regen = load_regen_version()
    state = regen.git_state(root)
    return state.get('commit') or ''


def load_records(records_dir):
    """
    Load all corpus records from JSONL shards in a directory

    :param records_dir: directory containing *.jsonl files
    :return: list of record dicts
    :raises ValueError: on malformed JSON with shard name in message
    """

    records = []
    records_path = Path(records_dir)
    shards = sorted(records_path.glob('*.jsonl'))

    for shard in shards:
        text = shard.read_text()
        for line_no, line in enumerate(text.splitlines(), start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                records.append(json.loads(stripped))
            except json.JSONDecodeError as exc:
                msg = '%s:%d: %s' % (shard.name, line_no, exc)
                raise ValueError(msg) from exc

    return records


def apply_view(records, transform, provenance):
    """
    Apply a view transform to corpus records

    :param records: list of corpus record dicts
    :param transform: view transform callable
    :param provenance: dict passed to each transform call
    :return: list of output row dicts
    """

    rows = []
    for record in records:
        row = transform(record, provenance)
        if row is not None:
            rows.append(row)
    return rows


def write_parquet(rows, output_dir, filename='train.parquet'):
    """
    Write rows to a parquet file under output_dir

    :param rows: list of row dicts
    :param output_dir: destination directory
    :param filename: parquet filename
    :return: path to written parquet file
    """

    out_path = Path(output_dir)
    out_path.mkdir(parents=True, exist_ok=True)
    parquet_path = out_path / filename
    table = pa.Table.from_pylist(rows)
    pq.write_table(table, parquet_path)
    return parquet_path


def export_corpus(
        view,
        records_dir,
        schema_path,
        output_dir,
        root=None):
    """
    Export one corpus dataset view to parquet

    :param view: view name registered in views.VIEWS
    :param records_dir: directory containing JSONL shards
    :param schema_path: path to SCHEMA.json
    :param output_dir: directory for train.parquet
    :param root: repository root for git provenance
    :return: path to written parquet file
    :raises KeyError: when view is unknown
    """

    views = load_views()
    transform = views.VIEWS[view]
    root = project_root(root)

    provenance = {
        'schema_version': read_schema_version(schema_path),
        'git_ref': git_ref(root),
    }

    records = load_records(records_dir)
    rows = apply_view(records, transform, provenance)
    return write_parquet(rows, output_dir)


def main(argv=None):
    """
    Export corpus views from the command line

    :param argv: optional argument list; defaults to sys.argv
    :return: integer exit code
    """

    root = project_root()
    default_records = root / 'docs' / 'corpus' / 'records'
    default_schema = root / 'docs' / 'corpus' / 'SCHEMA.json'
    default_view = 'koskari-eval'
    default_output = root / 'build' / 'corpus' / 'datasets' / default_view

    parser = argparse.ArgumentParser(
        prog='corpus-export',
        description='Export corpus JSONL records to parquet dataset views')
    parser.add_argument(
        '--view',
        default=default_view,
        help='dataset view name (default: %(default)s)')
    parser.add_argument(
        '--records-dir',
        default=str(default_records),
        help='directory of JSONL shards (default: %(default)s)')
    parser.add_argument(
        '--schema',
        default=str(default_schema),
        help='path to SCHEMA.json (default: %(default)s)')
    parser.add_argument(
        '--output-dir',
        default=str(default_output),
        help='output directory for train.parquet (default: %(default)s)')
    args = parser.parse_args(argv)

    views = load_views()
    if args.view not in views.VIEWS:
        names = ', '.join(sorted(views.VIEWS))
        sys.stderr.write('unknown view %r; available: %s\n' % (args.view, names))
        return 2

    try:
        path = export_corpus(
            args.view,
            args.records_dir,
            args.schema,
            args.output_dir,
            root=root)
    except ValueError as exc:
        sys.stderr.write('%s\n' % exc)
        return 1

    sys.stdout.write('%s\n' % path)
    return 0


if __name__ == '__main__':
    sys.exit(main())


# The end.
