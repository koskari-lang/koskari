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
tools.corpus.views.eval

koskari-eval dataset view: source plus verify.eval expectations.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


VIEW_NAME = 'koskari-eval'


def transform(record, provenance):
    """
    Build one koskari-eval row from a corpus record

    :param record: parsed corpus record dict
    :param provenance: dict with schema_version and git_ref keys
    :return: row dict, or None when verify.eval is absent
    """

    verify = record.get('verify') or {}
    eval_value = verify.get('eval')
    if eval_value is None:
        return None

    return {
        'corpus_id': record['id'],
        'feature': record['feature'],
        'source': record['source'],
        'eval': eval_value,
        'schema_version': provenance['schema_version'],
        'git_ref': provenance['git_ref'],
    }


# The end.
