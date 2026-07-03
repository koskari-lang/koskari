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
tools.corpus.views.full

koskari-corpus dataset view: every record with all verify stages.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.6 via Cursor
"""


VIEW_NAME = 'koskari-corpus'

_STAGES = ('read', 'expand', 'eval', 'error', 'invalid')


def transform(record, provenance):
    """
    Build one koskari-corpus row from a corpus record

    :param record: parsed corpus record dict
    :param provenance: dict with schema_version and git_ref keys
    :return: row dict for every record
    """

    verify = record.get('verify') or {}
    row = {
        'corpus_id': record['id'],
        'feature': record['feature'],
        'source': record['source'],
        'references': record.get('references') or [],
        'stability': record.get('stability'),
        'tags': record.get('tags') or [],
        'schema_version': provenance['schema_version'],
        'git_ref': provenance['git_ref'],
    }
    for stage in _STAGES:
        row[stage] = verify.get(stage)
    return row


# The end.
