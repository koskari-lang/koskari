"""
koskari.compile

The koskari compiler. Converts source forms into bytecode fragments.

author: Christopher O'Brien <obriencj@preoccupied.net>
license: GNU General Public License v3
"""

from .compiler import Compiler
from .exceptions import CompilerException, CompilerMode, UncaughtCompilerException
from .fragment import ByteCodeFragment
from .specials import SPECIAL_BYTECODE_SYMBOL, SPECIALS, special_bytecode


__all__ = (
    "Compiler",
    "ByteCodeFragment",
    "CompilerException",
    "CompilerMode",
    "UncaughtCompilerException",
    "SPECIAL_BYTECODE_SYMBOL",
    "SPECIALS",
    "special_bytecode",
)


# The end.
