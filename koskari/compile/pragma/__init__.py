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
koskari.compile.pragma

Compiler pragma definition. The pragma defines behavioral variations
that determine how the compiler operates on code.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from .resolution import (KoskariPragma, PythonPragma, ResolutionPragma,
                         SchemePragma)
from .settings import BracketCalls, ResolutionMode


__all__ = (
    'ResolutionPragma',
    'KoskariPragma',
    'PythonPragma',
    'SchemePragma',

    'BracketCalls',
    'ResolutionMode',
)


# The end.
