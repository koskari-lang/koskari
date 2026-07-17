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
tools.corpus.quick

Quick oracle and validator for corpus generation. Thin CLI over the
existing record runner in ``tests/corpus`` and the YAML serializer in
``tools/corpus/load.py`` -- it does not reimplement read, expand, eval,
verify, or serialization logic, so its output matches what the corpus
test phase accepts.

Intended to run via ``uv run`` against a worktree's own ``.venv`` (built
with ``make venv``), not through tox; see
``design/corpus-generation-agent.md`` and ``docs/corpus/CLAUDE.md``.

Subcommands:

  verify    run a source form through the implementation and emit
            corpus-encoded read/expand/eval strings (advisory)
  insert    schema-check, dedupe, run every verify stage, and append a
            candidate record to a shard (authoritative)
  coverage  report features with fewer than N entries

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import argparse
import importlib.util
import json
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))


# Importing the runner triggers the koskari bootstrap (the warmup cost
# that justifies one shared script rather than three).
from tests.corpus import (  # noqa: E402
    HANDLERS,
    CorpusTestCase,
    default_records_dir,
    default_schema_path,
    eval_values_equal,
    forms_equal,
    load_corpus_index,
    load_schema,
    parse_eval_expected,
)
from koskari.lib import car, cdr, kos_repr, nil  # noqa: E402
from koskari.types import is_pair, is_proper, is_symbol, unpack  # noqa: E402


def _load_py(path, name):
    """
    Load a standalone .py file as a module by path

    :param path: filesystem path to the source file
    :param name: module name to register under
    :return: the loaded module
    """

    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_load = _load_py(REPO_ROOT / 'tools' / 'corpus' / 'load.py', 'corpus_load')


# Categories whose entries route to a known shard when --shard is omitted.
_CATEGORY_TO_SHARD = {
    'conditional': 'conditionals',
    'function': 'functions',
    'class': 'classes',
    'variable': 'variables',
    'object': 'objects',
    'iter': 'iter',
    'syntax': 'syntax',
    'special': 'specials',
    'macro': 'syntax',
}


def _make_case(feature=None, block=False, fixtures=None):
    """
    Build a bound CorpusTestCase for an ad-hoc source form

    :param feature: optional feature string (selects the reader)
    :param block: when True, force the layout/block reader
    :param fixtures: optional extra eval bindings to merge
    :return: a bound CorpusTestCase
    """

    record = {
        'id': '<quick>',
        'feature': feature or '',
        'source': '',
        'tags': ['surface:block'] if block else [],
    }
    case = CorpusTestCase('bind_record')
    case.bind_record(record)
    if fixtures:
        for key, val in fixtures.items():
            case.module_ns[key] = val
    return case


def render_form(form):
    """
    Render a read form back to its s-expression text

    Uses kos_repr rather than str so that keywords render with their
    trailing colon (``count:`` not ``count``) and the text reads back to
    the same structure.

    :param form: a read/expanded form
    :return: source text that reads back to the same structure
    """

    return kos_repr(form)


def _eval_candidates(got):
    """
    Yield candidate corpus eval encodings for a runtime value

    Each candidate is validated by the caller against the real
    comparator before being trusted, so over-generating is harmless.

    :param got: the runtime eval result
    :return: iterator of candidate encoding strings
    """

    if got is None or got is nil:
        yield 'null'
        return
    if got is True:
        yield 'true'
        return
    if got is False:
        yield 'false'
        return
    if is_symbol(got):
        yield 'symbol:%s' % str(got)
        return
    if isinstance(got, (int, float)):
        try:
            yield json.dumps(got)
        except (TypeError, ValueError):
            pass
        return
    if isinstance(got, str):
        yield json.dumps(got)
        return
    if isinstance(got, list):
        try:
            yield 'list:%s' % json.dumps(got, separators=(',', ':'))
        except (TypeError, ValueError):
            pass
        return
    if isinstance(got, tuple):
        try:
            yield 'tuple:(%s)' % ','.join(
                json.dumps(x, separators=(',', ':')) for x in got)
        except (TypeError, ValueError):
            pass
        return
    if is_pair(got):
        yield from _pair_candidates(got)
        return
    try:
        yield json.dumps(got)
    except (TypeError, ValueError):
        pass


def _scalar_token(value):
    """
    Render one pair element as a proper/improper-list token

    :param value: a list element (symbol or json scalar)
    :return: token text
    :raises TypeError: when the element cannot be encoded
    """

    if is_symbol(value):
        return str(value)
    return json.dumps(value)


def _pair_candidates(got):
    """
    Yield proper-list/improper-list encodings for a pair value

    :param got: a pair value
    :return: iterator of candidate encoding strings
    """

    try:
        if is_proper(got):
            items = list(unpack(got))
            yield 'proper-list:%s' % json.dumps(
                items, separators=(',', ':'))
        else:
            parts = []
            cur = got
            while is_pair(cur):
                parts.append(_scalar_token(car(cur)))
                cur = cdr(cur)
            parts.append(_scalar_token(cur))
            head = ' '.join(parts[:-1])
            yield 'improper-list:(%s . %s)' % (head, parts[-1])
    except (TypeError, ValueError):
        return


def suggest_eval_encoding(got):
    """
    Return a corpus eval encoding that is verified to match the value

    Only an encoding that round-trips through the real parse + compare
    helpers is returned, so a non-None result is always correct.

    :param got: the runtime eval result
    :return: an encoding string, or None when none could be verified
    """

    for cand in _eval_candidates(got):
        try:
            kind, payload = parse_eval_expected(cand)
        except Exception:
            continue
        if kind != 'literal':
            continue
        try:
            if eval_values_equal(got, payload):
                return cand
        except Exception:
            continue
    return None


def _stage_read(case, source, warnings):
    """
    Produce the read-stage encoding for a source form
    """

    form = case.read_source(source)
    text = render_form(form)
    reread = case.parse_expected_form(text)
    if not forms_equal(form, reread):
        warnings.append('read string did not round-trip; verify by hand')
    return text


def _stage_expand(case, source, warnings):
    """
    Produce the expand-stage encoding for a source form
    """

    form = case.read_source(source)
    expanded = case.macroexpand1(form)
    text = render_form(expanded)
    reread = case.parse_expected_form(text)
    if not forms_equal(expanded, reread):
        warnings.append('expand string did not round-trip; verify by hand')
    return text


def _stage_eval(case, source, result):
    """
    Produce the eval-stage encoding for a source form

    Writes the verified encoding under "eval" when one is found;
    otherwise records the raw repr and type for manual encoding.
    """

    got = case.eval_source(source)
    encoding = suggest_eval_encoding(got)
    if encoding is not None:
        result['eval'] = encoding
    else:
        result['eval_raw'] = repr(got)
        result['eval_type'] = type(got).__name__
        result.setdefault('warnings', []).append(
            'could not auto-encode eval result; choose a literal or @marked:'
            ' encoding by hand (see docs/corpus/SCHEMA.md) and confirm with'
            ' "quick.py insert --dry-run"')


def cmd_verify(args):
    """
    verify subcommand: run a form and emit corpus-encoded strings

    :param args: parsed CLI arguments
    :return: process exit code
    """

    source = args.form
    if source is None:
        source = sys.stdin.read()
    source = source.strip()
    if not source:
        print(json.dumps({'error': 'empty source', 'stage': 'read'}))
        return 1

    stages = []
    if args.all or (not args.read and not args.expand and not args.eval):
        stages = ['read', 'expand', 'eval']
    else:
        if args.read:
            stages.append('read')
        if args.expand:
            stages.append('expand')
        if args.eval:
            stages.append('eval')

    fixtures = json.loads(args.fixture) if args.fixture else None
    case = _make_case(
        feature=args.feature, block=args.block, fixtures=fixtures)

    result = {}
    warnings = []
    errors = {}
    for stage in stages:
        try:
            if stage == 'read':
                result['read'] = _stage_read(case, source, warnings)
            elif stage == 'expand':
                result['expand'] = _stage_expand(case, source, warnings)
            elif stage == 'eval':
                _stage_eval(case, source, result)
        except Exception as exc:
            errors[stage] = '%s: %s' % (type(exc).__name__, exc)

    if warnings:
        result.setdefault('warnings', []).extend(warnings)
    if errors:
        result['errors'] = errors

    single = len(stages) == 1
    if single and stages[0] in errors:
        payload = {'error': errors[stages[0]], 'stage': stages[0]}
        print(json.dumps(payload))
        return 1

    produced = any(
        k in result for k in ('read', 'expand', 'eval', 'eval_raw'))
    print(json.dumps(result, indent=2))
    return 0 if produced else 1


def _read_candidate(args):
    """
    Read a candidate record from --file or stdin

    :param args: parsed CLI arguments
    :return: the parsed record dict
    """

    if args.file:
        text = Path(args.file).read_text()
    else:
        text = sys.stdin.read()
    import yaml
    doc = yaml.safe_load(text)
    if not isinstance(doc, dict):
        raise ValueError('candidate is not a single YAML mapping')
    return doc


def _verify_stages(record):
    """
    Run every applicable verify stage for a record

    :param record: candidate record dict
    :raises Exception: when a stage fails (assertion or otherwise)
    """

    verify = record.get('verify') or {}
    case = CorpusTestCase('bind_record')
    case.bind_record(record)
    ran = 0
    for stage, expected in verify.items():
        if stage == 'invalid':
            continue
        handler = HANDLERS.get(stage)
        if handler is None:
            raise ValueError('no handler for verify stage %r' % stage)
        handler(case, expected)
        ran += 1
    if not ran:
        raise ValueError('record has no runnable verify stages')


def _category_of(record):
    """
    Return the routing category tag for a record, or None
    """

    for tag in record.get('tags') or []:
        if ':' not in tag:
            return tag
    return None


def _target_shard(record, records_dir, override):
    """
    Resolve the shard path a record should be appended to

    :param record: candidate record dict
    :param records_dir: records directory path
    :param override: explicit --shard value, or None
    :return: shard Path
    :raises ValueError: when the shard cannot be inferred
    """

    if override:
        name = override if override.endswith('.yaml') else override + '.yaml'
        return Path(records_dir) / name

    category = _category_of(record)
    base = _CATEGORY_TO_SHARD.get(category)
    if base is None:
        raise ValueError(
            'cannot infer shard from tags %r; pass --shard'
            % (record.get('tags'),))

    verify = record.get('verify') or {}
    invalid = (
        record.get('stability') == 'invalid'
        or 'error' in verify or 'invalid' in verify)
    if invalid:
        inv = Path(records_dir) / ('%s-invalid.yaml' % base)
        if inv.exists():
            return inv
    return Path(records_dir) / ('%s.yaml' % base)


def _append_record(path, record):
    """
    Append one record as a new YAML document to a shard file

    Existing documents are left byte-for-byte unchanged.

    :param path: shard Path
    :param record: candidate record dict
    """

    doc = _load.dump_record(record)
    if path.exists() and path.read_text().strip():
        text = path.read_text()
        if not text.endswith('\n'):
            text += '\n'
        new = '%s---\n%s\n' % (text, doc)
    else:
        new = '%s\n' % doc
    path.write_text(new)


def cmd_insert(args):
    """
    insert subcommand: validate and append a candidate record

    :param args: parsed CLI arguments
    :return: process exit code
    """

    try:
        record = _read_candidate(args)
    except Exception as exc:
        print('FAIL <unparsed> %s: %s' % (type(exc).__name__, exc))
        return 1

    record_id = record.get('id', '<no-id>')

    try:
        import jsonschema
        schema = load_schema(default_schema_path())
        jsonschema.Draft202012Validator(schema).validate(record)
    except Exception as exc:
        print('FAIL %s schema: %s' % (record_id, exc))
        return 1

    try:
        index = load_corpus_index()
    except Exception as exc:
        print('FAIL %s corpus-load: %s' % (record_id, exc))
        return 1
    if record_id in index:
        print('FAIL %s duplicate id' % record_id)
        return 1

    try:
        _verify_stages(record)
    except Exception as exc:
        print('FAIL %s verify: %s: %s' % (
            record_id, type(exc).__name__, exc))
        return 1

    if args.dry_run:
        print('OK %s (validated, not written)' % record_id)
        return 0

    try:
        shard = _target_shard(record, default_records_dir(), args.shard)
        _append_record(shard, record)
    except Exception as exc:
        print('FAIL %s write: %s' % (record_id, exc))
        return 1

    rel = shard.relative_to(REPO_ROOT)
    print('OK %s (inserted into %s)' % (record_id, rel))
    return 0


def cmd_coverage(args):
    """
    coverage subcommand: report sparsely covered features

    :param args: parsed CLI arguments
    :return: process exit code
    """

    try:
        index = load_corpus_index()
    except Exception as exc:
        print('error loading corpus: %s' % exc)
        return 1

    counts = {}
    ids_by_feature = {}
    for record in index.values():
        feature = record.get('feature') or '<none>'
        counts[feature] = counts.get(feature, 0) + 1
        ids_by_feature.setdefault(feature, []).append(record['id'])

    if args.feature:
        n = counts.get(args.feature, 0)
        print('%s\t%d' % (args.feature, n))
        for rid in sorted(ids_by_feature.get(args.feature, [])):
            print('  %s' % rid)
        return 0

    rows = [(c, f) for f, c in counts.items() if c < args.min_entries]
    rows.sort()
    for count, feature in rows:
        print('%d\t%s' % (count, feature))
    print(
        '# %d features below %d entries (of %d features with any coverage)'
        % (len(rows), args.min_entries, len(counts)),
        file=sys.stderr)
    print(
        '# note: lists only features that already appear; gap analysis vs'
        ' the full special/macro registry is not yet implemented',
        file=sys.stderr)
    return 0


def build_parser():
    """
    Construct the argument parser

    :return: configured ArgumentParser
    """

    parser = argparse.ArgumentParser(
        prog='quick.py',
        description='Corpus oracle, validator, and coverage query.')
    sub = parser.add_subparsers(dest='command', required=True)

    pv = sub.add_parser(
        'verify', help='run a form and emit corpus-encoded strings')
    pv.add_argument(
        'form', nargs='?',
        help='source form; read from stdin when omitted')
    pv.add_argument('--read', action='store_true', help='emit read stage')
    pv.add_argument(
        '--expand', action='store_true', help='emit expand stage')
    pv.add_argument('--eval', action='store_true', help='emit eval stage')
    pv.add_argument(
        '--all', action='store_true',
        help='emit all stages (default when none selected)')
    pv.add_argument(
        '--block', action='store_true',
        help='read with the layout/block reader')
    pv.add_argument(
        '--feature', help='feature string (influences reader selection)')
    pv.add_argument(
        '--fixture', help='JSON object of extra eval bindings')
    pv.set_defaults(func=cmd_verify)

    pi = sub.add_parser(
        'insert', help='validate and append a candidate record')
    pi.add_argument(
        '--file', help='candidate YAML file; reads stdin when omitted')
    pi.add_argument(
        '--shard', help='target shard name (recommended; inferred if absent)')
    pi.add_argument(
        '--dry-run', action='store_true',
        help='validate only; do not write')
    pi.set_defaults(func=cmd_insert)

    pc = sub.add_parser(
        'coverage', help='report sparsely covered features')
    pc.add_argument(
        '--min-entries', type=int, default=3,
        help='report features with fewer than N entries (default 3)')
    pc.add_argument(
        '--feature', help='report the count and ids for one feature')
    pc.set_defaults(func=cmd_coverage)

    return parser


def main(argv=None):
    """
    CLI entry point

    :param argv: optional argument vector
    :return: process exit code
    """

    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())


# The end.
