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
import: compiler/resolution, compiler/call-brackets, compiler/stray-colon,
compiler/keyword-literals, compiler/tco-enable, compiler/cache-expression,
compiler/cache-function, compiler/protect-code, compiler/pack-module,
compiler/rewrite-limit, and the reserved compiler/convert-fast and
compiler/compress. Each setting's
``default`` is its baseline (value when undeclared); the function's
argument defaults are the parameter defaults (value when declared but the
arg is omitted).

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


@pragma_setting("compiler/call-brackets", BracketCalls.WARN)
def compiler_call_brackets(level=BracketCalls.WARN) -> BracketCalls:
    """
    Policy when a bracket-pair reaches call position.

    "warn" - (default) compile it, but emit a SyntaxWarning
    "error" - raise a CompilerError
    "ignore" - compile it exactly like a paren form, no diagnostic
    """

    if isinstance(level, BracketCalls):
        return level
    return BracketCalls(str(level))


class StrayColon(Enum):
    """
    How the compiler treats a bare ``:`` symbol reaching compilation as a
    name. Nothing binds ``:``, so it is a mistake in every case -- almost
    always a half-converted layout form, where ``(for-each [x (xs)]: body)``
    mixes the layout block marker into an s-expression and reads as
    ``(for-each [x (xs)] : body)``.

    - ``ALLOW``: compile it as an ordinary name lookup, no diagnostic
    - ``WARN``: compile it, but emit a SyntaxWarning
    - ``ERROR``: raise a CompilerError

    ``ALLOW`` rather than ``IGNORE`` (the spelling ``compiler/call-brackets``
    uses) because "ignore the colon" would read as dropping it.
    """

    ALLOW = "allow"
    WARN = "warn"
    ERROR = "error"


@pragma_setting("compiler/stray-colon", StrayColon.WARN)
def compiler_stray_colon(level=StrayColon.WARN) -> StrayColon:
    """
    Policy when a bare ``:`` symbol is compiled as a name.

    "warn" - (default) compile it, but emit a SyntaxWarning
    "error" - raise a CompilerError
    "allow" - compile it as an ordinary name lookup, no diagnostic
    """

    if isinstance(level, StrayColon):
        return level
    return StrayColon(str(level))


class KeywordLiterals(Enum):
    """
    How the compiler treats a keyword reaching evaluated position, where it
    compiles to itself as a constant. Writing the literal unquoted reads as
    a value but is far more often a typo -- ``(if foo: 1 2)`` tests the
    truthiness of the keyword ``foo:``, which is always true, rather than
    reading the variable ``foo``.

    - ``ALLOW``: compile it as a constant, no diagnostic
    - ``WARN``: compile it, but emit a SyntaxWarning
    - ``ERROR``: raise a CompilerError

    Quoting states the intent and is always exempt: ``'foo:`` compiles
    through the quote special and never reaches this check. Keyword
    arguments are structural, not values, so ``(f bar: 1)`` is exempt too.
    """

    ALLOW = "allow"
    WARN = "warn"
    ERROR = "error"


@pragma_setting("compiler/keyword-literals", KeywordLiterals.WARN)
def compiler_keyword_literals(level=KeywordLiterals.WARN) -> KeywordLiterals:
    """
    Policy when an unquoted keyword is compiled as a literal value.

    "warn" - (default) compile it, but emit a SyntaxWarning
    "error" - raise a CompilerError
    "allow" - compile it as a constant, no diagnostic
    """

    if isinstance(level, KeywordLiterals):
        return level
    return KeywordLiterals(str(level))


@pragma_setting("compiler/rewrite-limit", 32)
def compiler_rewrite_limit(limit=32) -> int:
    """
    The full_compile rewrite-loop settling limit: how many expansion
    rewrites one form may take before the compiler reports divergent
    growth. 0 means unlimited; the equality cycle check applies
    regardless. Baseline 32 -- legitimate expansion chains are
    single-digit, and a divergent expansion grows costlier with every
    rewrite, so the ceiling stays low.
    """

    limit = int(limit)
    if limit < 0:
        raise ValueError(
            f"rewrite-limit must be >= 0, not {limit}")
    return limit


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


@pragma_setting("compiler/pack-module", True)
def compiler_pack_module(enabled: bool = True) -> bool:
    """
    Whether function code compiled during a module load is placed into
    the load's arena chunks, which are sealed read-only together when
    the load completes. Expression code is never placed. Baseline
    enabled.
    """

    return enabled


# The end.
