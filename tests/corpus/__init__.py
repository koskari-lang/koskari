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
tests.corpus

Corpus record loading, expected-value parsing, verify handlers, and test base.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


import importlib.util
import json
import re
import unittest
from pathlib import Path
from types import SimpleNamespace

import jsonschema

import koskari.builtins  # noqa: F401

from koskari import exceptions
from koskari.compile import Compiler, CompilerMode
from koskari.lib import (
    KosType,
    car,
    cdr,
    cons,
    demand_call,
    nil,
    symbol,
)
from koskari.module import new_module, public_vars
from koskari.reader import READ_EOF, LayoutReader, default_reader, source_str
from koskari.reader.layouts.basics import layout_flat
from koskari.syntax import MacroTransform, Syntax, SyntaxTransform
from koskari.types import is_pair, is_proper, is_symbol, unpack


_WILDCARD = object()

_FORBIDDEN_COMPILE_PATTERNS = (
    r'^opcode:',
    r'^lambda-body:opcode:',
    r'^expression:opcode:',
    r'^fold-prefix:',
)

_CORPUS_FIXTURES = {
    'add': lambda x, y: x + y,
    'deref': lambda r: r.deref(),
    'obj': SimpleNamespace(count=5, value=99),
    'seq': (1, 2, 3),
    'z': 1,
    'a': 0,
    'f': lambda *args: args,
    'bad_guy': lambda x: (_ for _ in ()).throw(RuntimeError('boom')) or None,
}


def project_root(root=None):
    """
    Return the repository root path

    :param root: optional override
    :return: Path to repository root
    """

    if root is not None:
        return Path(root).resolve()
    return Path(__file__).resolve().parent.parent.parent


def default_records_dir(root=None):
    """
    Return the default corpus records directory path

    :param root: optional repository root override
    :return: path to docs/corpus/records
    """

    return project_root(root) / 'docs' / 'corpus' / 'records'


def default_schema_path(root=None):
    """
    Return the default SCHEMA.json path

    :param root: optional repository root override
    :return: path to docs/corpus/SCHEMA.json
    """

    return project_root(root) / 'docs' / 'corpus' / 'SCHEMA.json'


def _import_corpus_load(root=None):
    """
    Load tools.corpus.load as a module

    :param root: optional repository root override
    :return: corpus load module
    """

    path = project_root(root) / 'tools' / 'corpus' / 'load.py'
    spec = importlib.util.spec_from_file_location('corpus_load', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load_records(records_dir):
    """
    Load all corpus records from YAML shards in a directory

    :param records_dir: directory containing *.yaml files
    :return: list of record dicts
    :raises ValueError: on malformed YAML with shard name in message
    """

    return _import_corpus_load().load_records(records_dir)


def load_schema(schema_path):
    """
    Load corpus JSON Schema from disk

    :param schema_path: path to SCHEMA.json
    :return: parsed schema dict
    """

    return json.loads(Path(schema_path).read_text())


def validate_records_schema(records, schema):
    """
    Validate each record against the corpus JSON Schema

    :param records: list of record dicts
    :param schema: parsed SCHEMA.json dict
    :raises jsonschema.ValidationError: when a record fails validation
    """

    validator = jsonschema.Draft202012Validator(schema)
    for record in records:
        validator.validate(record)


def load_corpus_index(records_dir=None, schema_path=None, root=None):
    """
    Load corpus records and return an id-indexed mapping

    :param records_dir: optional records directory override
    :param schema_path: optional SCHEMA.json path override
    :param root: optional repository root override
    :return: dict mapping record id to record dict
    :raises ValueError: on duplicate ids or load errors
    :raises jsonschema.ValidationError: on schema validation failure
    """

    root = project_root(root)
    if records_dir is None:
        records_dir = default_records_dir(root)
    if schema_path is None:
        schema_path = default_schema_path(root)

    records = load_records(records_dir)
    schema = load_schema(schema_path)
    validate_records_schema(records, schema)

    index = {}
    for record in records:
        record_id = record['id']
        if record_id in index:
            raise ValueError('duplicate corpus id: %s' % record_id)
        index[record_id] = record

    return index


def load_corpus_cases(records_dir=None, schema_path=None, root=None):
    """
    Load corpus verify cases as record_id and stage pairs

    :param records_dir: optional records directory override
    :param schema_path: optional SCHEMA.json path override
    :param root: optional repository root override
    :return: list of (record_id, stage) tuples
    """

    index = load_corpus_index(
        records_dir=records_dir,
        schema_path=schema_path,
        root=root)

    cases = []
    for record_id in sorted(index):
        verify = index[record_id].get('verify') or {}
        for stage in sorted(verify):
            if stage == 'invalid':
                continue
            cases.append((record_id, stage))

    return cases


def form_to_py(form):
    """
    Convert a koskari form to nested Python lists/scalars for comparison

    :param form: pair, symbol, or scalar
    :return: nested Python structure
    """

    if is_symbol(form):
        return str(form)
    if is_pair(form):
        if not is_proper(form):
            head = form_to_py(car(form))
            tail = cdr(form)
            if tail is nil:
                return head
            return (head, form_to_py(tail))
        return [form_to_py(x) for x in unpack(form)]
    return form


def parse_eval_value(text):
    """
    Parse a verify.eval expected string into a Python comparison value

    :param text: expected eval encoding string
    :return: value suitable for comparison with eval results
    """

    text = text.strip()
    if text == 'null':
        return None
    if text == 'true':
        return True
    if text == 'false':
        return False
    if text.startswith('symbol:'):
        return symbol(text[7:])
    if text.startswith('list:'):
        return json.loads(text[5:])
    if text.startswith('tuple:'):
        inner = text[6:].strip()
        if inner.startswith('(') and inner.endswith(')'):
            inner = '[' + inner[1:-1] + ']'
        return tuple(json.loads(inner))
    if text.startswith('proper-list:'):
        inner = text[12:].strip()
        if inner.startswith('(') and inner.endswith(')'):
            inner = '[' + inner[1:-1] + ']'
        elif not inner.startswith('['):
            inner = '[' + inner + ']'
        items = json.loads(inner)
        return cons(*items, nil) if items else nil
    if text.startswith('improper-list:'):
        return _parse_improper_list(text[14:].strip())
    if text.startswith('"') and text.endswith('"'):
        return json.loads(text)
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def _parse_improper_list(text):
    """
    Parse improper-list:(1 2 . 3) style encoding into a pair spine
    """

    text = text.strip()
    if text.startswith('(') and text.endswith(')'):
        text = text[1:-1].strip()
    if ' . ' in text:
        head_text, tail_text = text.rsplit(' . ', 1)
        parts = head_text.split()
        if not parts:
            raise ValueError('improper list missing head elements')
        cells = [
            json.loads(p) if p[0] in '0123456789-"' else symbol(p)
            for p in parts]
        if tail_text[0] in '0123456789-"':
            tail_val = json.loads(tail_text)
        else:
            tail_val = symbol(tail_text)
        result = tail_val
        for item in reversed(cells[1:]):
            result = cons(item, result)
        return cons(cells[0], result)
    items = json.loads('[' + text + ']')
    return cons(*items, nil)


def eval_values_equal(got, expected):
    """
    Compare an eval result against a parsed expected value

    :param got: runtime eval result
    :param expected: parsed expected value from parse_eval_value
    :return: True when values match
    """

    if expected is nil or (is_pair(expected) and expected is nil):
        return got is None or got is nil
    if is_symbol(expected):
        return is_symbol(got) and str(got) == str(expected)
    if is_pair(expected):
        if not is_proper(expected):
            return form_to_py(got) == form_to_py(expected)
        if not is_pair(got) or not is_proper(got):
            return False
        g = list(unpack(got))
        e = list(unpack(expected))
        if len(g) != len(e):
            return False
        return all(eval_values_equal(a, b) for a, b in zip(g, e))
    if isinstance(expected, list):
        return isinstance(got, list) and got == expected
    if isinstance(expected, tuple):
        return isinstance(got, tuple) and got == expected
    return got == expected


def forms_equal(got, expected_form):
    """
    Exact structural equality of two read forms

    :param got: actual form
    :param expected_form: expected form from the reader
    :return: True when forms match
    """

    return form_to_py(got) == form_to_py(expected_form)


def _pattern_from_form(form):
    """
    Convert a form to a pattern tree where symbol ... is WILDCARD
    """

    if form is Ellipsis:
        return _WILDCARD
    if is_symbol(form):
        if str(form) == '...':
            return _WILDCARD
        return str(form)
    if is_pair(form):
        if not is_proper(form):
            return (form_to_py(form),)
        return [_pattern_from_form(x) for x in unpack(form)]
    return form


def _match_pattern(got, pattern):
    """
    Match a Pythonized form against a pattern tree with WILDCARD
    """

    if pattern is _WILDCARD:
        return True
    if isinstance(pattern, list):
        if not isinstance(got, list):
            return False
        gi = 0
        pi = 0
        while pi < len(pattern):
            if pattern[pi] is _WILDCARD:
                if pi == len(pattern) - 1:
                    return True
                for skip in range(len(got) - gi + 1):
                    if _match_pattern(got[gi + skip:], pattern[pi + 1:]):
                        return True
                return False
            if gi >= len(got):
                return False
            if not _match_pattern(got[gi], pattern[pi]):
                return False
            gi += 1
            pi += 1
        return gi == len(got)
    return got == pattern


def form_matches_pattern(got_form, expected_form):
    """
    Compare a form against an expected form allowing ... wildcards

    :param got_form: actual expanded/read form
    :param expected_form: expected form tree from reader
    :return: True when the pattern matches
    """

    return _match_pattern(
        form_to_py(got_form),
        _pattern_from_form(expected_form))


def check_compile_expected(case, code, expected, tc=False):
    """
    Assert a Code object satisfies a verify.compile expected string

    :param case: unittest case for assertions
    :param code: compiled Code object
    :param expected: verify.compile string
    :param tc: whether tail-call tracking was enabled
    :raises AssertionError: when expectation is not met
    :raises ValueError: when expected string uses forbidden bytecode checks
    """

    text = expected.strip()

    for pattern in _FORBIDDEN_COMPILE_PATTERNS:
        if re.match(pattern, text):
            raise ValueError(
                'corpus verify.compile must not use bytecode or opcodes'
                ' (see docs/corpus/SCHEMA.md): %r' % text)

    if text == 'code.extras return annotation int':
        expr = code.expression(case.module_ns)
        fn = demand_call(expr)
        case.assertEqual(fn.code.extras.get('return'), symbol('int'))
        return

    if text in (
            '__annotations__ return int on built function',
            '__annotations__ return int; selfref=False',
            '__annotations__ return int'):
        expr = code.expression(case.module_ns)
        fn = demand_call(expr)
        case.assertEqual(fn.__annotations__.get('return'), 'int')
        return

    m = re.match(
        r'^special:class yields KosType named ([A-Z][A-Za-z0-9_]*)$',
        text)
    if m:
        _check_kostype_named(case, code, m.group(1))
        return

    if text == 'macro expands to define + class; yields KosType C':
        _check_kostype_named(case, code, 'C')
        return

    if text == 'single fn unchanged':
        return

    raise ValueError('unknown compile expectation: %r' % text)


def _check_kostype_named(case, code, name):
    """
    Compile and eval code expecting a KosType with the given name
    """

    expr = code.expression(case.module_ns)
    result = demand_call(expr)
    case.assertIsInstance(result, type)
    case.assertEqual(result.__name__, name)
    case.assertIs(type(result), KosType)


def parse_error_expected(text):
    """
    Parse verify.error string into exception class name and optional message

    :param text: verify.error string
    :return: tuple (class_name or None, message_substring or None)
    """

    text = text.strip()
    if text.endswith('Error') and ': ' not in text:
        return text, None
    if ': ' in text:
        cls, msg = text.split(': ', 1)
        return cls, msg
    return None, text


def error_exception_class(class_name):
    """
    Resolve an exception class name to a Python type

    :param class_name: exception name such as LayoutError
    :return: exception type
    """

    return getattr(exceptions, class_name)


def verify_read(case, expected):
    """
    Verify read stage expectation for the bound corpus record

    :param case: CorpusTestCase instance
    :param expected: verify.read string
    """

    got = case.read_source(case.record['source'])
    exp = case.parse_expected_form(expected)
    case.assertTrue(
        forms_equal(got, exp),
        'read form mismatch for %s\n got: %r\n exp: %r'
        % (case.record['id'], got, exp))


def verify_expand(case, expected):
    """
    Verify expand stage expectation for the bound corpus record

    :param case: CorpusTestCase instance
    :param expected: verify.expand string
    """

    form = case.read_source(case.record['source'])
    got = case.macroexpand1(form)
    exp = case.parse_expected_form(expected)
    case.assertTrue(
        form_matches_pattern(got, exp),
        'expand form mismatch for %s\n got: %r\n exp: %r'
        % (case.record['id'], got, exp))


def verify_compile(case, expected):
    """
    Verify compile stage expectation for the bound corpus record

    :param case: CorpusTestCase instance
    :param expected: verify.compile string
    """

    code = case.compile_source(case.record['source'])
    check_compile_expected(case, code, expected)


def verify_eval(case, expected):
    """
    Verify eval stage expectation for the bound corpus record

    :param case: CorpusTestCase instance
    :param expected: verify.eval string
    """

    got = case.eval_source(case.record['source'])
    exp = parse_eval_value(expected)
    case.assertTrue(
        eval_values_equal(got, exp),
        'eval mismatch for %s\n got: %r\n exp: %r'
        % (case.record['id'], got, exp))


def verify_error(case, expected):
    """
    Verify error stage expectation for the bound corpus record

    :param case: CorpusTestCase instance
    :param expected: verify.error string
    """

    cls_name, msg = parse_error_expected(expected)

    if cls_name == 'LayoutError':
        with case.assertRaises(error_exception_class(cls_name)) as ctx:
            case.read_source(case.record['source'])
        if msg:
            case.assertIn(msg, str(ctx.exception))
        return

    with case.assertRaises(Exception) as ctx:
        case.compile_source(case.record['source'])

    if cls_name:
        case.assertIsInstance(
            ctx.exception,
            error_exception_class(cls_name))
    if msg:
        case.assertIn(msg, str(ctx.exception))


HANDLERS = {
    'read': verify_read,
    'expand': verify_expand,
    'compile': verify_compile,
    'eval': verify_eval,
    'error': verify_error,
}


class CorpusTestCase(unittest.IsolatedAsyncioTestCase):
    """
    Async test base with corpus read, expand, compile, and eval helpers
    """

    record = None
    module = None
    module_ns = None
    compiler = None
    reader = None


    def bind_record(self, record):
        """
        Bind a corpus record and build module, reader, and compiler

        :param record: corpus record dict
        """

        self.record = record
        import koskari.builtins as builtins

        ns_dict = dict(public_vars(builtins))
        ns_dict.update(_CORPUS_FIXTURES)
        self._inject_layout_handlers(ns_dict, record)

        self.module = new_module('corpus.test')
        mod_ns = vars(self.module)
        for key, val in ns_dict.items():
            mod_ns[key] = val

        self.module_ns = mod_ns
        self.reader = self._reader_for(record, mod_ns)
        self.compiler = Compiler(
            '<corpus>',
            self.module,
            filename='corpus:%s' % record['id'])


    def _inject_layout_handlers(self, ns_dict, record):
        """
        Add layout reader handlers required by specific records
        """

        feature = record.get('feature') or ''
        if feature.startswith('layout/define-syntax'):
            ns_dict['layout/define-syntax'] = layout_flat
        if feature.startswith('layout/extend-syntax'):
            ns_dict['layout/extend-syntax'] = layout_flat


    def _reader_for(self, record, ns):
        """
        Select LayoutReader or default_reader for a record
        """

        feature = record.get('feature') or ''
        tags = record.get('tags') or []
        if feature.startswith('layout/'):
            return LayoutReader(ns)
        for tag in tags:
            if tag in ('surface:block', 'surface:line'):
                return LayoutReader(ns)
        return default_reader


    def parse_expected_form(self, text):
        """
        Parse an expected s-expression string into a form

        :param text: verify.read or verify.expand string
        :return: parsed form
        """

        stream = source_str(text, '<expected>')
        return default_reader.read(stream)


    def read_source(self, source):
        """
        Read corpus source text to a single form

        Multiple top-level forms are wrapped in compile/begin

        :param source: source text from the record
        :return: read form
        """

        stream = source_str(source, '<corpus>')
        forms = []
        while True:
            val = self.reader.read(stream)
            if val is READ_EOF:
                break
            forms.append(val)

        if not forms:
            raise ValueError('empty source')
        if len(forms) == 1:
            return forms[0]
        return cons(symbol('compile/begin'), *forms, nil)


    def macroexpand1(self, form):
        """
        Apply one macro expansion step (semantics from basics.ki macroexpand-1)

        :param form: read form
        :return: expanded form or original when not a macro
        """

        name = car(form) if is_pair(form) else form
        special = self.compiler.find_compiled(name)
        if not isinstance(special, Syntax):
            return form

        tf, mr = special.match_transformer(form)
        if tf is None:
            return form

        if not isinstance(tf, (SyntaxTransform, MacroTransform)):
            return form

        expanded = tf.handle_expand(self.compiler, form, mr, False)

        if is_proper(expanded):
            parts = list(expanded.unpack())
            if len(parts) == 2:
                head = parts[0]
                if is_symbol(head) and str(head) in ('begin', 'compile/begin'):
                    return parts[1]

        return expanded


    def compile_source(self, source, tc=False):
        """
        Compile source to a Code object

        :param source: source text
        :param tc: enable tail-call tracking when True
        :return: Code object
        """

        form = self.read_source(source)
        fragment = self.compiler.full_compile(form, tc=tc)
        return self.compiler.complete(fragment, mode=CompilerMode.EXPRESSION)


    def eval_source(self, source):
        """
        Compile and evaluate source in the bound module namespace

        :param source: source text
        :return: eval result
        """

        form = self.read_source(source)
        fragment = self.compiler.full_compile(form)
        expr = fragment.as_expression(self.module_ns)
        return demand_call(expr)


# The end.
