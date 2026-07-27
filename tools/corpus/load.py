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
tools.corpus.load

Load and dump Koskari corpus records as multi-document YAML shards.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import re
from pathlib import Path

import yaml


_RECORD_FIELD_ORDER = (
    'id',
    'feature',
    'source',
    'verify',
    'references',
    'stability',
    'tags',
    'notes',
)

_VERIFY_KEY_ORDER = (
    'read',
    'expand',
    'compile',
    'eval',
    'error',
    'invalid',
)

_YAML_BOOL_WORDS = frozenset({
    'null',
    'true',
    'false',
    'yes',
    'no',
    'on',
    'off',
    '~',
})

_NUMBER_RE = re.compile(r'^-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?$')


def load_records(records_dir):
    """
    Load all corpus records from YAML shards in a directory

    :param records_dir: directory containing *.yaml files
    :return: list of record dicts
    :raises ValueError: on malformed YAML with shard name in message
    """

    records = []
    records_path = Path(records_dir)
    shards = sorted(records_path.glob('*.yaml'))

    for shard in shards:
        text = shard.read_text()
        try:
            docs = list(yaml.safe_load_all(text))
        except yaml.YAMLError as exc:
            msg = '%s: %s' % (shard.name, exc)
            raise ValueError(msg) from exc

        for doc_no, doc in enumerate(docs, start=1):
            if doc is None:
                continue
            if not isinstance(doc, dict):
                msg = '%s:document %d: expected mapping, got %s' % (
                    shard.name,
                    doc_no,
                    type(doc).__name__,
                )
                raise ValueError(msg)
            records.append(doc)

    return records


def _escape_double_quoted(text):
    """
    Escape a string for YAML double-quoted scalar form
    """

    return text.replace('\\', '\\\\').replace('"', '\\"')


def _yaml_needs_double_quotes(text):
    """
    Return True when text must be double-quoted to round-trip as a string
    """

    if not text:
        return True
    if text in ('?', '*', '&'):
        return True
    lowered = text.lower()
    if lowered in _YAML_BOOL_WORDS:
        return True
    if _NUMBER_RE.match(text):
        return True
    if '#' in text:
        return True
    if ':' in text:
        return True
    if text[0] in '@`&*!|>\\%':
        return True
    if text.strip() != text:
        return True
    return False


def _use_block_scalar(text):
    """
    Return True when text should be emitted as a literal block scalar
    """

    return '\n' in text


def _format_inline_scalar(text):
    """
    Format a single-line string for YAML output
    """

    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return "'%s'" % text.replace("'", "''")
    if _yaml_needs_double_quotes(text):
        return '"%s"' % _escape_double_quoted(text)
    return text


def _format_scalar_lines(key, text, indent):
    """
    Format one key and string value as YAML lines
    """

    prefix = '  ' * indent
    if _use_block_scalar(text):
        lines = [prefix + '%s: |-' % key]
        for line in text.split('\n'):
            lines.append(prefix + '  ' + line)
        return lines
    return [prefix + '%s: %s' % (key, _format_inline_scalar(text))]


def _format_verify(verify, indent):
    """
    Format the verify mapping for YAML output
    """

    prefix = '  ' * indent
    lines = [prefix + 'verify:']
    for key in _VERIFY_KEY_ORDER:
        if key not in verify:
            continue
        lines.extend(_format_scalar_lines(key, verify[key], indent + 1))
    return lines


def _format_list(key, values, indent):
    """
    Format a list field as a YAML block sequence.

    An empty list needs the explicit flow form: a bare ``key:`` parses
    back as None, which fails schema validation for the whole shard on
    the next append.
    """

    prefix = '  ' * indent
    if not values:
        return [prefix + '%s: []' % key]
    lines = [prefix + '%s:' % key]
    for value in values:
        lines.append(prefix + '  - %s' % _format_inline_scalar(value))
    return lines


def dump_record(record):
    """
    Serialize one corpus record as a YAML document string

    :param record: corpus record dict
    :return: YAML document text without leading document marker
    """

    lines = []
    for field in _RECORD_FIELD_ORDER:
        if field not in record:
            continue
        value = record[field]
        if field == 'verify':
            lines.extend(_format_verify(value, 0))
        elif field in ('references', 'tags'):
            lines.extend(_format_list(field, value, 0))
        elif field == 'source':
            lines.extend(_format_scalar_lines(field, value, 0))
        elif field == 'notes':
            lines.extend(_format_scalar_lines(field, value, 0))
        else:
            lines.append('%s: %s' % (field, _format_inline_scalar(value)))
    return '\n'.join(lines)


def dump_shard(records):
    """
    Serialize corpus records as a multi-document YAML shard

    :param records: list of corpus record dicts
    :return: full shard text
    """

    docs = [dump_record(record) for record in records]
    return '\n---\n'.join(docs) + '\n'


def write_shard(path, records):
    """
    Write corpus records to a YAML shard file

    :param path: destination path
    :param records: list of corpus record dicts
    """

    Path(path).write_text(dump_shard(records))


# The end.
