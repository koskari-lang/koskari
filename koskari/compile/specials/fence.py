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
koskari.compile.specials.fence

Special form handler for compile/apply-fence.

Expands a fenced foreign literal at compile time: resolves the
fence/<name> handler in the module namespace, calls it with the raw
fenced source and metadata, and compiles the returned source in place.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...exceptions import SpecialError
from ...fences import FenceHandler, find_fence, meta_plist_to_dict
from ...lib import demand_call, nil
from ...matcher import CALLEDBY, CAPTURE, MATCH, PAIR, SYMBOL, TYPE
from ...reader.layouts import apply_layout
from ...syntax import special
from ...types import fill_position, is_keyword, is_pair, is_symbol


_APPLY_FENCE_PATTERNS = [
    MATCH(
        CALLEDBY(),
        SYMBOL("name"),
        CAPTURE("raw", TYPE("str")),
        PAIR("meta")),

    MATCH(
        CALLEDBY(),
        SYMBOL("name"),
        CAPTURE("raw", TYPE("str"))),
]


def _is_source(value):
    """
    True when value is usable as a Koskari source form
    """

    if value is None:
        return False
    if is_pair(value) or is_symbol(value) or is_keyword(value):
        return True
    return isinstance(value, (str, bytes, bool, int, float, complex))


@builtin("compile/apply-fence")
@special("compile/apply-fence", patterns=_APPLY_FENCE_PATTERNS,
         unpack="match")
def special_apply_fence(compiler, caps):
    """
    Compile (compile/apply-fence NAME RAW META) — fence expansion.

    Resolves fence/<NAME> in the module namespace, calls the handler
    with the raw fenced source text and a metadata dict, and compiles
    the source form it returns in place of the fence. META is an
    optional keyword plist carrying filename:, line:, col:, and
    options: entries.
    """

    pos = caps["called-by"].position

    name = str(caps["name"].datum)
    raw = caps["raw"].datum

    meta = caps.get("meta")
    meta_form = meta.datum if meta is not None else nil

    try:
        meta_dict = meta_plist_to_dict(
            meta_form, name=name, filename=compiler.filename)
    except ValueError as ve:
        raise SpecialError(
            ve.args[0], position=pos,
            filename=compiler.filename) from None

    cvars = vars(compiler.module)
    handler = find_fence(cvars, name)
    if handler is None:
        raise SpecialError(
            f"unknown fence: fence/{name}", position=pos,
            filename=compiler.filename)

    if isinstance(handler, FenceHandler):
        result = handler.read(raw, meta_dict)
    else:
        result = demand_call(handler, raw, meta_dict)

    if not _is_source(result):
        raise SpecialError(
            f"fence/{name} returned non-source: {result!r}",
            position=pos, filename=compiler.filename)

    fill_position(result, pos)

    return apply_layout(result, cvars)


# The end.
