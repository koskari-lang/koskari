# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this library; if not, see <http://www.gnu.org/licenses/>.


"""
tests.lexer.prompt_toolkit.test_completer

Tests for the REPL's tab completer

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


import pytest
from prompt_toolkit.completion import CompleteEvent
from prompt_toolkit.document import Document

from koskari.lexer.prompt_toolkit.completer import KoskariCompleter


pytestmark = pytest.mark.phase_infra


@pytest.mark.parametrize(
    'text, prefix, expected',
    [
        ('(my-v', 'my-v', 'my-var'),
        ('(os/path-j', 'os/path-j', 'os/path-join'),
        ('(,@my-v', 'my-v', 'my-var'),
    ])
def test_completion_covers_whole_name(text, prefix, expected):
    """
    A completion replaces the whole atom before the cursor
    """

    completer = KoskariCompleter(
        get_namespace=lambda: {'my-var': 1, 'os/path-join': 2})
    found = list(completer.get_completions(Document(text), CompleteEvent()))

    assert [(c.text, c.start_position) for c in found] == \
        [(expected, -len(prefix))]


# The end.
