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
koskari.fences.json_

The built-in fence/json handler: extended JSON fenced literals.

The json fence accepts standard JSON with one narrow extension: a bare
identifier in value position lowers to an ordinary Koskari name
reference. Object keys remain quoted strings; ``true``, ``false``, and
``null`` always mean the JSON literals. No expressions, interpolation,
or dotted member access.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from json import loads as _json_loads
from re import compile as regex

from ..bootstrap import builtin
from ..exceptions import FenceError
from ..lib import cons, nil, symbol
from ..types import set_position
from .helpers import fence, meta_position, opener_position


_symbol_dict = symbol("#dict")
_symbol_list = symbol("#list")
_symbol_tuple = symbol("#tuple")
_symbol_true = symbol("True")
_symbol_false = symbol("False")
_symbol_none = symbol("None")

_JSON_LITERALS = {
    "true": _symbol_true,
    "false": _symbol_false,
    "null": _symbol_none,
}

_string_re = regex(r'"(?:\\.|[^"\\])*"').match
_number_re = regex(r"-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?").match
_ident_re = regex(r"[A-Za-z_][A-Za-z0-9_-]*").match


class _JsonSourceParser:
    """
    Recursive-descent parser lowering extended JSON to Koskari source.

    Tracks payload-relative positions and remaps them through the fence
    META so diagnostics and generated forms point into the fence body.
    """

    def __init__(self, text, meta):
        self.text = text
        self.meta = meta
        self.pos = 0
        self.line = 1
        self.col = 0


    def error(self, message, at=None):
        """
        Build a FenceError at the given or current payload position
        """

        line, col = at if at else (self.line, self.col)
        return FenceError(
            message,
            position=meta_position(self.meta, line, col),
            filename=self.meta.get("filename"))


    def _advance(self, n):
        seg = self.text[self.pos:self.pos + n]
        newlines = seg.count("\n")
        if newlines:
            self.line += newlines
            self.col = len(seg) - seg.rfind("\n") - 1
        else:
            self.col += n
        self.pos += n


    def _skip_ws(self):
        text = self.text
        while self.pos < len(text) and text[self.pos] in " \t\r\n":
            self._advance(1)


    def _peek(self):
        if self.pos < len(self.text):
            return self.text[self.pos]
        return ""


    def _expect(self, char, message):
        if self._peek() != char:
            raise self.error(message)
        self._advance(1)


    def _positioned(self, form, at):
        set_position(form, meta_position(self.meta, *at))
        return form


    def parse(self):
        """
        Parse the whole payload into one Koskari source form
        """

        self._skip_ws()
        if not self._peek():
            raise self.error("empty JSON fence")
        value = self.parse_value()
        self._skip_ws()
        if self._peek():
            raise self.error("trailing content after JSON value")
        return value


    def parse_value(self):
        """
        Parse one JSON value into a Koskari source form
        """

        c = self._peek()
        if c == "{":
            return self.parse_object()
        if c == "[":
            return self.parse_array()
        if c == '"':
            return self.parse_string()
        if c == "-" or c.isdigit():
            return self.parse_number()
        if _ident_re(self.text, self.pos):
            return self.parse_identifier()
        raise self.error(f"unexpected character {c!r} in JSON fence")


    def parse_string(self):
        """
        Parse a JSON string token into a str literal
        """

        at = (self.line, self.col)
        matched = _string_re(self.text, self.pos)
        if not matched:
            raise self.error("unterminated or invalid JSON string", at)
        token = matched.group()
        self._advance(len(token))
        try:
            return _json_loads(token)
        except ValueError:
            raise self.error("invalid JSON string escape", at) from None


    def parse_number(self):
        """
        Parse a JSON number token into an int or float literal
        """

        at = (self.line, self.col)
        matched = _number_re(self.text, self.pos)
        if not matched:
            raise self.error("invalid JSON number", at)
        token = matched.group()
        self._advance(len(token))
        if any(c in token for c in ".eE"):
            return float(token)
        return int(token)


    def parse_identifier(self):
        """
        Parse a bare identifier into a JSON literal or name reference
        """

        at = (self.line, self.col)
        matched = _ident_re(self.text, self.pos)
        token = matched.group()
        self._advance(len(token))
        if self._peek() == ".":
            raise self.error(
                "dotted member access is not allowed in JSON fences", at)
        found = _JSON_LITERALS.get(token)
        if found is not None:
            return found
        return symbol(token)


    def parse_object(self):
        """
        Parse a JSON object into a (#dict (#tuple KEY VALUE) ...) form
        """

        at = (self.line, self.col)
        self._advance(1)
        entries = []

        self._skip_ws()
        while self._peek() != "}":
            key_at = (self.line, self.col)
            c = self._peek()
            if not c:
                raise self.error("unterminated JSON object", at)
            if c != '"':
                if _ident_re(self.text, self.pos):
                    raise self.error(
                        "JSON object keys must be quoted strings", key_at)
                raise self.error("expected a JSON object key", key_at)
            key = self.parse_string()

            self._skip_ws()
            self._expect(":", "expected ':' after JSON object key")
            self._skip_ws()
            value = self.parse_value()

            entries.append(self._positioned(
                cons(_symbol_tuple, key, value, nil), key_at))

            self._skip_ws()
            if self._peek() == ",":
                self._advance(1)
                self._skip_ws()
                if self._peek() == "}":
                    raise self.error("trailing comma in JSON object")
            elif self._peek() != "}":
                raise self.error("expected ',' or '}' in JSON object")

        self._advance(1)
        return self._positioned(cons(_symbol_dict, *entries, nil), at)


    def parse_array(self):
        """
        Parse a JSON array into a (#list VALUE ...) form
        """

        at = (self.line, self.col)
        self._advance(1)
        values = []

        self._skip_ws()
        while self._peek() != "]":
            if not self._peek():
                raise self.error("unterminated JSON array", at)
            values.append(self.parse_value())

            self._skip_ws()
            if self._peek() == ",":
                self._advance(1)
                self._skip_ws()
                if self._peek() == "]":
                    raise self.error("trailing comma in JSON array")
            elif self._peek() != "]":
                raise self.error("expected ',' or ']' in JSON array")

        self._advance(1)
        return self._positioned(cons(_symbol_list, *values, nil), at)


@builtin("fence/json")
@fence("json")
def fence_json(raw, meta):
    """
    fence/json handler: lower an extended JSON fence to Koskari source

    Standard JSON plus one narrow extension: a bare identifier in value
    position lowers to an ordinary Koskari name reference. Object keys
    remain quoted strings; true, false, and null always mean the JSON
    literals.
    """

    if meta.get("options"):
        names = ", ".join(sorted(meta["options"]))
        raise FenceError(
            f"fence/json accepts no options, got: {names}",
            position=opener_position(meta),
            filename=meta.get("filename"))

    return _JsonSourceParser(raw, meta).parse()


# The end.
