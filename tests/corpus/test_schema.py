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
Structural validation of corpus records against SCHEMA.json.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import unittest

import pytest

from tests.corpus import (
    default_records_dir, default_schema_path, load_corpus_index, load_records,
    load_schema, validate_records_schema)


pytestmark = pytest.mark.phase_corpus


class TestCorpusSchema(unittest.TestCase):

    def test_all_records_validate_against_schema(self):
        """
        Every YAML record satisfies docs/corpus/SCHEMA.json
        """

        records = load_records(default_records_dir())
        schema = load_schema(default_schema_path())
        validate_records_schema(records, schema)


    def test_corpus_index_has_unique_ids(self):
        """
        load_corpus_index rejects duplicate record ids
        """

        index = load_corpus_index()
        self.assertGreater(len(index), 0)


# The end.
