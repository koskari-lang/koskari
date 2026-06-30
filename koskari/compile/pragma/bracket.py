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
koskari.compile.pragma.bracket

Policy for a bracket-pair appearing in call position. A future pragma DSL
can surface this; for now it is a compiler flag.

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
:ai-assistant: claude-opus-4-8 via Claude Code
"""

from enum import Enum


__all__ = ("BracketCalls",)


class BracketCalls(Enum):
    """
    How the compiler treats a bracket-pair (``[ ]``) reaching call /
    operator position — a ``[ ]`` form being compiled as a combination or
    special, where ``( )`` is conventionally expected.

    - ``IGNORE``: compile it exactly like a paren form, no diagnostic
    - ``WARN``: compile it, but emit a SyntaxWarning
    - ``ERROR``: raise a CompilerError
    """

    IGNORE = "ignore"
    WARN = "warn"
    ERROR = "error"


# The end.
