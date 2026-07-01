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
koskari.reader.pragma

Reader-side pragma settings, registered into the koskari.pragma registry at
import: reader/brackets, reader/layout, reader/implicit-calls. Each setting's
``default`` is its baseline (value when undeclared); the function's argument
defaults are the parameter defaults (value when declared but the arg is
omitted).

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
:ai-assistant: claude-opus-4-8 via Claude Code
"""


from ..pragma import pragma_setting


@pragma_setting("reader/brackets", True)
def reader_brackets(enabled: bool = True) -> bool:
    """
    Whether ``[ ]`` reads into distinct bracket-pair / bracket-nil values
    (baseline on) rather than plain pairs
    """

    return enabled


@pragma_setting("reader/layout", True)
def reader_layout(enabled: bool = True) -> bool:
    """
    Whether layout (indentation) block reading is enabled (baseline on)
    """

    return enabled


@pragma_setting("reader/implicit-calls", True)
def reader_implicit_calls(enabled: bool = True) -> bool:
    """
    Whether multiple values on one layout line form an implicit call
    (baseline on)
    """

    return enabled


# The end.
