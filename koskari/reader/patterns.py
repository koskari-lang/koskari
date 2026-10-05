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
koskari.reader.patterns

Literal-recognition regexes and the string-prefix set shared by the reader
and the lexer's tokenizer. The reader is authoritative for how source is
actually parsed; the tokenizer classifies the same atoms for highlighting
and completion, so both import these to stay in lockstep.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from re import compile as regex


__all__ = (
    "INTEGER_RE",
    "BIN_RE",
    "OCT_RE",
    "HEX_RE",
    "FLOAT_RE",
    "DECIMAL_RE",
    "FRACTION_RE",
    "COMPLEX_RE",
    "KEYWORD_RE",
    "STRING_PREFIXES",
)


INTEGER_RE = regex(r"^-?\d+$")
BIN_RE = regex(r"^0b[01]+$")
OCT_RE = regex(r"^0o[0-7]+$")
HEX_RE = regex(r"^0x[\da-f]+$")
FLOAT_RE = regex(r"^-?("
                 r"(\d*\.\d+|\d+\.\d*)(e-?\d+)?f?"
                 r"|(\d+e-?\d+)f?"
                 r"|\d+f"
                 ")$")
DECIMAL_RE = regex(r"^-?((\d*\.\d+)|(\d+\.\d*)|\d+)d$")
FRACTION_RE = regex(r"^-?\d+/\d+$")
COMPLEX_RE = regex(r"^-?\d*\.?\d+\+\d*\.?\d*[ij]$")
KEYWORD_RE = regex(r"^(:.+|.+:)$")

# String-prefix characters the reader registers reader macros for; a prefix
# is only recognised when immediately followed by a quote. Must match the
# ``f`` / ``r`` / ``b`` macros installed in Reader._add_default_macros.
STRING_PREFIXES = frozenset("frb")


# The end.
