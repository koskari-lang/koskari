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
koskari.compile.pragma.settings

Compiler-side pragma settings, registered into the koskari.pragma registry at
import: compiler/resolution, compiler/call-brackets, compiler/tco-enable, and
the reserved compiler/convert-fast and compiler/compress. Each setting's
``default`` is its baseline (value when undeclared); the function's argument
defaults are the parameter defaults (value when declared but the arg is
omitted).

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from enum import Enum

from ...pragma import pragma_setting
from .resolution import KoskariPragma, PythonPragma, SchemePragma


class ResolutionMode(Enum):
    """
    Resolution modes: Koskari, Scheme, Python
    """

    KOSKARI = "koskari"
    SCHEME = "scheme"
    PYTHON = "python"


ResolutionMode.KOSKARI.resolution_class = KoskariPragma
ResolutionMode.SCHEME.resolution_class = SchemePragma
ResolutionMode.PYTHON.resolution_class = PythonPragma


@pragma_setting("compiler/resolution", ResolutionMode.KOSKARI)
def compiler_resolution(mode) -> ResolutionMode:
    """
    Name-resolution semantics: koskari (strict), scheme, or python. No
    parameter default -- the mode must be stated explicitly when declared.
    """

    if isinstance(mode, ResolutionMode):
        return mode
    return ResolutionMode(str(mode))


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


@pragma_setting("compiler/call-brackets", BracketCalls.IGNORE)
def compiler_call_brackets(level=BracketCalls.WARN) -> BracketCalls:
    """
    Policy when a bracket-pair reaches call position. Baseline ignore; a bare
    declaration enables it at warn.
    """

    if isinstance(level, BracketCalls):
        return level
    return BracketCalls(str(level))


@pragma_setting("compiler/tco-enable", True)
def compiler_tco_enable(enabled: bool = True) -> bool:
    """
    Whether tail-call optimization is enabled. Baseline enabled.
    """

    return enabled


@pragma_setting("compiler/convert-fast", True)
def compiler_convert_fast(enabled: bool = True) -> bool:
    """
    Lowers VAR-based ops to FAST-based ops where possible. Baseline enabled.
    """

    return enabled


@pragma_setting("compiler/compress", False)
def compiler_compress(enabled: bool = False) -> bool:
    """
    Reserved: the bytecode compression pass is not yet built. Inert until
    implemented.
    """

    return enabled


# The end.
