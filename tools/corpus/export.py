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

Export corpus YAML records to parquet dataset views.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import argparse
import importlib.util
import json
import sys
from pathlib import Path

import pyarrow as pa
import pyarrow.parquet as pq


# Run as a script, sys.path[0] is this directory rather than the
# repository root, so the tools package is not importable without help.
REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


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

    The view modules are written in Koskari, so importing them goes
    through the koskari import hook rather than a path-based loader.

    :return: views module with VIEWS registry
    """

    import koskari  # noqa: F401  (activates the .ki import hook)

    from tools.corpus import views

    return views


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
    Load all corpus records from YAML shards in a directory

    The loader is written in Koskari, so importing it goes through the
    koskari import hook rather than a path-based loader.

    :param records_dir: directory containing *.yaml files
    :return: list of record dicts
    :raises ValueError: on malformed YAML with shard name in message
    """

    import koskari  # noqa: F401  (activates the .ki import hook)

    from tools.corpus.load import load_records as _load_records

    return _load_records(records_dir)


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
    :param records_dir: directory containing YAML shards
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


def export_source_view(
        view,
        schema_path,
        output_dir,
        root=None):
    """
    Export one source-generated view to parquet

    Source views build rows from repository files (docs, example programs)
    rather than from corpus records, so they receive the repository root and
    provenance instead of the loaded record list.

    :param view: view name registered in views.SOURCE_VIEWS
    :param schema_path: path to SCHEMA.json
    :param output_dir: directory for train.parquet
    :param root: repository root for git provenance
    :return: path to written parquet file
    :raises KeyError: when view is unknown
    """

    views = load_views()
    generate = views.SOURCE_VIEWS[view]
    root = project_root(root)

    provenance = {
        'schema_version': read_schema_version(schema_path),
        'git_ref': git_ref(root),
    }

    rows = generate(root, provenance)
    return write_parquet(rows, output_dir)


def export_all_views(
        records_dir,
        schema_path,
        output_base,
        root=None):
    """
    Export every registered corpus and source view to parquet

    :param records_dir: directory containing YAML shards
    :param schema_path: path to SCHEMA.json
    :param output_base: parent directory for per-view subdirectories
    :param root: repository root for git provenance
    :return: list of paths to written parquet files
    """

    views = load_views()
    paths = []
    for view_name in sorted(views.VIEWS):
        output_dir = Path(output_base) / view_name
        path = export_corpus(
            view_name,
            records_dir,
            schema_path,
            output_dir,
            root=root)
        paths.append(path)
    for view_name in sorted(views.SOURCE_VIEWS):
        output_dir = Path(output_base) / view_name
        path = export_source_view(
            view_name,
            schema_path,
            output_dir,
            root=root)
        paths.append(path)
    return paths


def main(argv=None):
    """
    Export corpus views from the command line

    :param argv: optional argument list; defaults to sys.argv
    :return: integer exit code
    """

    root = project_root()
    default_records = root / 'docs' / 'corpus' / 'records'
    default_schema = root / 'docs' / 'corpus' / 'SCHEMA.json'
    default_view = 'koskari-corpus'
    default_output = root / 'build' / 'corpus' / 'datasets' / default_view
    default_output_base = root / 'build' / 'corpus' / 'datasets'

    parser = argparse.ArgumentParser(
        prog='corpus-export',
        description='Export corpus YAML records to parquet dataset views')
    parser.add_argument(
        '--all-views',
        action='store_true',
        help='export every registered view under the output base directory')
    parser.add_argument(
        '--view',
        default=default_view,
        help='dataset view name (default: %(default)s)')
    parser.add_argument(
        '--records-dir',
        default=str(default_records),
        help='directory of YAML shards (default: %(default)s)')
    parser.add_argument(
        '--schema',
        default=str(default_schema),
        help='path to SCHEMA.json (default: %(default)s)')
    parser.add_argument(
        '--output-dir',
        default=str(default_output),
        help='output directory for train.parquet (default: %(default)s)')
    parser.add_argument(
        '--output-base',
        default=str(default_output_base),
        help='parent directory for --all-views (default: %(default)s)')
    args = parser.parse_args(argv)

    views = load_views()

    try:
        if args.all_views:
            paths = export_all_views(
                args.records_dir,
                args.schema,
                args.output_base,
                root=root)
            for path in paths:
                sys.stdout.write('%s\n' % path)
            return 0

        if args.view in views.SOURCE_VIEWS:
            path = export_source_view(
                args.view,
                args.schema,
                args.output_dir,
                root=root)
            sys.stdout.write('%s\n' % path)
            return 0

        if args.view not in views.VIEWS:
            names = ', '.join(sorted(
                set(views.VIEWS) | set(views.SOURCE_VIEWS)))
            sys.stderr.write(
                'unknown view %r; available: %s\n' % (args.view, names))
            return 2

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
