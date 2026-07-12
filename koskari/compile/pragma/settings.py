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
import: compiler/resolution, compiler/call-brackets, compiler/tco-enable,
compiler/cache-expression, compiler/cache-function, compiler/protect-code,
compiler/pack-module, and the reserved compiler/convert-fast and
compiler/compress. Each setting's ``default`` is its baseline (value when
undeclared); the function's argument defaults are the parameter defaults
(value when declared but the arg is omitted).

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from enum import Enum
from typing import NamedTuple

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


@pragma_setting("compiler/cache-expression", False)
def compiler_cache_expression(enabled: bool = True) -> bool:
    """
    Whether expression code objects (compiled at the top level of a module
    load or a REPL step) get per-site opcode caches. Baseline disabled: a
    top-level expression typically runs once, so a cache never amortizes. A
    bare declaration enables it (mention-to-enable).
    """

    return enabled


@pragma_setting("compiler/cache-function", True)
def compiler_cache_function(enabled: bool = True) -> bool:
    """
    Whether function and lambda code objects (compiled inside a module or
    REPL) get per-site opcode caches. Baseline enabled: function bodies are
    the reused code that idiomatic method calls flow through, so caching
    pays off.
    """

    return enabled


@pragma_setting("compiler/protect-code", False)
def compiler_protect_code(enabled: bool = True) -> bool:
    """
    Whether each Code object's bytecode block is page-aligned and
    mprotect-sealed read-only at construction. Baseline disabled; a bare
    declaration enables it (mention-to-enable).
    """

    return enabled


class PackModule(NamedTuple):
    """
    Resolved compiler/pack-module setting: the on/off master switch and
    the arena sizing caps. A cap of 0 means unbounded.
    """

    enabled: bool
    max_count: int
    max_bytes: int


def _pack_module_cap(name, value):
    cap = int(value)
    if cap < 0:
        raise ValueError(
            f"pack-module option {name} must be >= 0, not {cap}")
    return cap


@pragma_setting("compiler/pack-module", PackModule(True, 0, 0))
def compiler_pack_module(enabled=True, **options) -> PackModule:
    """
    Whether the module's durable code objects are relocated into shared,
    singly-sealed arenas when the module seals. Baseline enabled with
    unbounded arenas (one arena per module).

    Options (0 means unbounded): ``max-count`` caps the number of code
    objects per arena; ``max-bytes`` is a soft cap on an arena's payload
    size -- a single larger code object still lands in its own singleton
    arena.
    """

    max_count = 0
    max_bytes = 0

    for key, value in options.items():
        norm = str(key).replace("_", "-")
        if norm == "max-count":
            max_count = _pack_module_cap(norm, value)
        elif norm == "max-bytes":
            max_bytes = _pack_module_cap(norm, value)
        else:
            raise ValueError(f"Unknown pack-module option {key}")

    return PackModule(bool(enabled), max_count, max_bytes)


# The end.
