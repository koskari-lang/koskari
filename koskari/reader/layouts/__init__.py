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

Handlers register via ``koskari.stdlib.builtin`` into ``koskari.stdlib.BUILTINS``.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.5 via Cursor
"""


from ...bootstrap import builtin
from ...lib import cons, keyword, nil
from ...types import build_proper, is_pair, is_symbol, unpack
from .basics import (layout_begin, layout_block, layout_block_begin,
                     layout_call, layout_flat, layout_quote,
                     layout_quasiquote)
from .classes import layout_class, layout_define_class
from .conditionals import layout_if, layout_switch
from .define_syntax import (layout_macro_match, layout_macro_rules,
                            layout_special_match, layout_special_rules,
                            layout_syntax_match, layout_syntax_rules)
from .dyn_wind import layout_dyn_wind
from .exceptions import layout_try
from .helpers import LayoutHandler, layout
from .loops import layout_for_each, layout_while


__all__ = (
    "layout",
    "LayoutHandler",

    "layout_block",
    "layout_block_begin",
    "layout_flat",
    "layout_begin",
    "layout_call",
    "layout_quote",
    "layout_quasiquote",
    "layout_class",
    "layout_define_class",
    "layout_dyn_wind",
    "layout_if",
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

    "apply_layouts",
    "find_layout",
)


def _find_layout(ns, name):
    v = ns.get(name)
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

    found = _find_layout(ns, name)
    if found is None:
        if not name.startswith("layout/"):
            return _find_layout(ns, "layout/" + name)
    return found


_keyword_block = keyword(':block:')


@builtin('apply-layouts')
def apply_layouts(blocks, namespace, reader=None):
    """
    Walk a proper cons spine of forms and expand each :block: through the
    layout handler namespace. Declared peer blocks are consumed from the cdr
    spine after each primary block; other forms are kept unchanged

    Intended for binding as apply-blocks at runtime

    :param blocks: proper cons list of forms
    :param namespace: mapping used for layout handler lookup
    :param reader: optional layout reader passed to layout handlers
    :return: proper cons list of expanded forms
    """

    from koskari.lib import kos_repr

    result = blocks

    while blocks:
        print("blocks: ", kos_repr(blocks))

        form, tail = blocks
        print("form: ", kos_repr(form))

        if not form or not is_pair(form):
            pass

        elif form.car is _keyword_block:
            (head_args, (body_exprs, _)) = form.cdr
            head = head_args.car

            layout_obj = None
            if is_symbol(head):
                layout_obj = find_layout(namespace, str(head))
            if layout_obj is None:
                layout_obj = find_layout(namespace, "layout/block")
            if layout_obj is None:
                raise RuntimeError(
                    "layout/block handler not found in module namespace")

            peers = []
            declared = {str(s) for s in layout_obj.peers}
            if declared:
                while tail:
                    sibling = tail.car
                    if not (sibling and is_pair(sibling)
                            and sibling.car is _keyword_block):
                        break

                    (sibling_head, (sibling_exprs, _)) = sibling.cdr
                    if str(sibling_head.car) not in declared:
                        break

                    peers.append(cons(sibling_head, sibling_exprs))
                    tail = blocks.cdr = tail.cdr

            if layout_obj.raw:
                source = cons(head_args, body_exprs)
                blocks.car = layout_obj.read(reader, source, peers)
            else:
                expanded_body = apply_layouts(body_exprs, namespace, reader)
                expanded_peers = []
                for peer in peers:
                    pheader, pbody = peer
                    expanded_pbody = apply_layouts(
                        pbody, namespace, reader)
                    expanded_peers.append(cons(pheader, expanded_pbody))
                source = cons(head_args, expanded_body)
                blocks.car = layout_obj.read(reader, source, expanded_peers)

        else:
            blocks.car = apply_layouts(form, namespace, reader)

        blocks = tail

    print("result: ", kos_repr(result))
    return result


@builtin('apply-layout')
def apply_layout(block, namespace, reader=None):
    """
    Apply a layout handler to a single block of form
    ```(:block: (HEAD ARG ...) (BODY ...))```

    :param block: the block to apply the layout handler to
    :param namespace: the namespace to use for layout handler lookup
    :param reader: optional layout reader passed to layout handlers
    :return: the layout form
    """

    if block and is_pair(block):
        if block.car is _keyword_block:
            res = apply_layouts(cons(block, nil), namespace, reader)
            return res.car
        else:
            return apply_layouts(block, namespace, reader)
    return block


# The end.
