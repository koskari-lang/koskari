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
koskari.reader.layouts

Layout handlers for reader block syntax.

Handlers register via ``koskari.bootstrap.builtin`` into its builtin
registry.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


from ...bootstrap import builtin
from ...lib import cons, keyword, nil, symbol
from ...types import (
    fill_position, get_position, is_keyword, is_pair, is_symbol, strip_mark)
from .basics import (
    layout_block, layout_block_begin, layout_import_from,
    layout_inline_block, layout_quasiquote, layout_quasiquote_syntax,
    layout_quote, layout_quote_syntax)
from .classes import (
    layout_class, layout_define_class, layout_define_dataclass)
from .conditionals import layout_if, layout_match, layout_switch
from .define_syntax import (
    layout_bare_syntax, layout_macro_match, layout_macro_rules,
    layout_special_match, layout_special_rules, layout_syntax_match,
    layout_syntax_rules)
from .dyn_wind import layout_dyn_wind
from .exceptions import layout_try
from .helpers import LayoutHandler, is_block, is_line, layout
from .loops import layout_for_each, layout_while


__all__ = (
    "layout",
    "LayoutHandler",

    "layout_block",
    "layout_block_begin",
    "layout_inline_block",

    "layout_bare_syntax",
    "layout_quote",
    "layout_quasiquote",
    "layout_quote_syntax",
    "layout_quasiquote_syntax",
    "layout_class",
    "layout_define_class",
    "layout_define_dataclass",
    "layout_dyn_wind",
    "layout_if",
    "layout_match",
    "layout_import_from",
    "layout_macro_match",
    "layout_macro_rules",
    "layout_special_match",
    "layout_special_rules",
    "layout_syntax_match",
    "layout_syntax_rules",
    "layout_switch",
    "layout_try",
    "layout_while",
    "layout_for_each",

    "apply_layout",
    "apply_layouts",
    "apply_lines",
    "find_layout",
    "is_block",
    "is_line",
    "reader_for",
)


def _find_layout(ns, name):
    # layout handlers live in the builtins, below a module namespace's
    # own values; a plain dict harness has no resolve and answers as-is
    lookup = getattr(ns, 'resolve', None) or ns.get
    v = lookup(name, None)
    if v and isinstance(v, LayoutHandler):
        return v
    return None


def find_layout(ns, name):
    """
    Resolve layout/<head> in the module namespace

    :param name: symbol or string for the block head (e.g. if, begin)
    :return: LayoutHandler instance, or None
    """

    name = str(name)

    # prefix-only resolution, mirroring find_compiled: the short
    # name is the use, the prefixed spelling is the handler object
    if name.startswith("layout/"):
        return None
    return _find_layout(ns, "layout/" + name)


_keyword_block = keyword('#block')
_keyword_line = keyword('#line')
_keyword_splice = keyword('#splice')
_symbol_begin = symbol('begin')


def reader_for(target):
    """
    The layout reader to expand with

    :param target: a LayoutReader, or a namespace whose module owns one
    :return: the LayoutReader
    """

    from ..layout import LayoutReader
    from ...module import find_module_reader

    if isinstance(target, LayoutReader):
        return target
    return find_module_reader(target)


def _gather_peers(tail, declared):
    """
    Consume declared peer blocks from the head of a spine

    :param tail: the spine after the primary block
    :param declared: set of peer head names the layout accepts
    :return: (peers, remaining tail) where each peer is the block's
        ``(header . entries)`` cdr, carrying the block's position
    """

    peers = []
    while tail:
        sibling = tail.car
        if not is_block(sibling):
            break
        if str(strip_mark(sibling.cdr.car.car)) not in declared:
            break
        peer = sibling.cdr
        peer_pos = get_position(sibling)
        if peer_pos and is_pair(peer):
            peer.set_position(peer_pos)
        peers.append(peer)
        tail = tail.cdr
    return peers, tail


def _lower_block(form, tail, reader):
    """
    Expand one :#block: cons through its layout handler

    :param form: the ``(:#block: header entry ...)`` cons
    :param tail: the spine after it, from which declared peers are taken
    :param reader: the LayoutReader expanding this source
    :return: (lowered form, remaining tail)
    """

    head_args, entries = form.cdr
    head = strip_mark(head_args.car)

    layout_obj = None
    if is_symbol(head):
        layout_obj = reader.find_layout(str(head))
    if layout_obj is None:
        layout_obj = reader.find_layout("block")
    if layout_obj is None:
        raise RuntimeError(
            "layout/block handler not found in module namespace")

    declared = {str(sym) for sym in layout_obj.peers}
    peers = []
    if declared:
        peers, tail = _gather_peers(tail, declared)

    block_pos = get_position(form)

    if layout_obj.raw:
        source = form.cdr
        lowered = layout_obj.read(reader, source, peers)
    else:
        expanded_peers = []
        for peer in peers:
            pheader, pentries = peer
            expanded = cons(pheader, apply_lines(pentries, reader))
            peer_pos = get_position(peer)
            if peer_pos:
                expanded.set_position(peer_pos)
            expanded_peers.append(expanded)
        source = cons(head_args, apply_lines(entries, reader))
        if block_pos:
            source.set_position(block_pos)
        lowered = layout_obj.read(reader, source, expanded_peers)

    # handlers build fresh conses; restamp the block position onto
    # any output cells that lack one (handlers stamp peer-derived
    # clauses with the peer's own position first)
    fill_position(lowered, block_pos)
    return lowered, tail


def apply_lines(entries, target):
    """
    Expand the sub-blocks of a body

    A body is a proper list of entries, each a line, ``(:#line:
    ELEMENT ...)`` over the elements the reader saw on it, or a nested
    :#block: cons. A sub-block whose head is not a keyword is lowered in
    place into a one-element line holding its form, taking declared peer
    blocks from the entries that follow it. A sub-block whose head is a
    keyword is left as it is for the handler. A ``(:#splice: FORM ...)``
    entry, left by a filled quasiquote block, spreads into one line per
    form. Any element of a line that is a pair is walked for embedded
    blocks

    :param entries: proper cons list of lines and blocks
    :param target: a LayoutReader or a namespace whose module owns one
    :return: proper cons list of entries with every sub-block lowered
    """

    reader = reader_for(target)
    out = []

    while entries:
        entry, tail = entries

        if is_block(entry):
            if is_keyword(entry.cdr.car.car):
                out.append(entry)
            else:
                lowered, tail = _lower_block(entry, tail, reader)
                line = cons(_keyword_line, lowered, nil)
                if get_position(entry):
                    line.set_position(get_position(entry))
                out.append(line)

        elif is_pair(entry) and entry.car is _keyword_splice:
            for form in entry.cdr.unpack():
                spread = cons(_keyword_line, form, nil)
                if get_position(entry):
                    spread.set_position(get_position(entry))
                out.append(spread)

        elif is_line(entry):
            corr = entry.cdr
            while corr:
                elem, corr_tail = corr
                if elem and is_pair(elem):
                    corr.car = apply_layouts(elem, reader)
                corr = corr_tail
            out.append(entry)

        else:
            out.append(entry)

        entries = tail

    return cons(*out, nil) if out else nil


@builtin('apply-layouts')
def apply_layouts(blocks, target):
    """
    Walk a proper cons spine of forms and expand each :#block: through the
    layout handler namespace. Declared peer blocks are consumed from the cdr
    spine after each primary block; other forms are kept unchanged

    Intended for binding as apply-blocks at runtime

    :param blocks: proper cons list of forms
    :param target: a LayoutReader or a namespace whose module owns one
    :return: proper cons list of expanded forms
    """

    reader = reader_for(target)
    result = blocks

    # a Datum in the spine is the caller's material, left for the peel
    # as a Datum car is
    while is_pair(blocks) and blocks:
        form, tail = blocks

        if not form or not is_pair(form):
            pass

        elif form.car is _keyword_block:
            lowered, tail = _lower_block(form, tail, reader)
            blocks.cdr = tail
            blocks.car = lowered

        else:
            blocks.car = apply_layouts(form, reader)

        blocks = tail

    return result


@builtin('apply-layout')
def apply_layout(block, target):
    """
    Apply a layout handler to a single block of form
    ```(:#block: (HEAD ARG ...) ENTRY ...)```

    :param block: the block to apply the layout handler to
    :param target: a LayoutReader or a namespace whose module owns one
    :return: the layout form
    """

    if block and is_pair(block):
        if block.car is _keyword_block:
            res = apply_layouts(cons(block, nil), target)
            return res.car
        else:
            return apply_layouts(block, target)
    return block


# The end.
