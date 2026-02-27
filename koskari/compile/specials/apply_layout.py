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
koskari.compile.specials.apply_layout

Special form handler for compile/apply-layout.

Accepts one or more :block: cons literals, depth-first expands them
through the layout handler system, and returns the resulting compiled
forms. Multiple top-level results are wrapped in compile/begin.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from ...exceptions import SpecialError
from ...types import car, cdr, cons, is_nil, is_pair, keyword, nil, symbol, unpack
from ..helpers import special


_keyword_block = keyword(':block:')
_symbol_begin = symbol('compile/begin')
_symbol_layout_block = symbol('layout/block')


def _is_block(form):
    """
    Return True if form is a :block: cons
    """

    return is_pair(form) and not is_nil(form) and car(form) is _keyword_block


def _expand_body(compiler, body_exprs):
    """
    Walk a body expression list, recursively expanding any :block: elements.
    Non-block elements pass through unchanged. Returns a Python list of
    expanded forms.

    :param compiler: the active Compiler
    :param body_exprs: proper cons list of body forms
    :return: Python list of expanded forms
    """

    items = list(unpack(body_exprs))
    result = []
    i = 0
    while i < len(items):
        form = items[i]
        if _is_block(form):
            expanded, consumed = _expand_block(compiler, form, items[i + 1:])
            result.append(expanded)
            i += 1 + consumed
        else:
            result.append(form)
            i += 1
    return result


def _expand_block(compiler, block, rest):
    """
    Depth-first expand a single :block: cons through its layout handler.

    Recursively expands any nested :block: elements in the body before
    invoking the layout handler. Scans rest for declared peer blocks and
    consumes them. Returns (expanded_form, peers_consumed_count).

    :param compiler: the active Compiler
    :param block: a :block: cons of the form (:block: HEAD-ARGS BODY-EXPRS)
    :param rest: Python list of sibling forms that follow block
    :return: tuple of (expanded_form, number_of_rest_items_consumed_as_peers)
    """

    block_tail = cdr(block)
    if is_nil(block_tail) or not is_pair(block_tail):
        raise SpecialError("malformed :block: — missing HEAD-ARGS", block)

    head_args = car(block_tail)
    body_tail = cdr(block_tail)
    if is_nil(body_tail) or not is_pair(body_tail):
        raise SpecialError("malformed :block: — missing BODY-EXPRS", block)

    body_exprs = car(body_tail)

    if is_nil(head_args) or not is_pair(head_args):
        raise SpecialError("malformed :block: — HEAD-ARGS must be a pair", block)

    head = car(head_args)

    expanded_body_items = _expand_body(compiler, body_exprs)
    if expanded_body_items:
        expanded_body = cons(*expanded_body_items, nil)
    else:
        expanded_body = nil

    layout_obj = compiler.find_layout(head)

    if layout_obj is None:
        layout_obj = compiler.find_layout(_symbol_layout_block)
        if layout_obj is None:
            raise SpecialError(
                "layout/block handler not found in module namespace",
                block)
        head_args = cons(_symbol_layout_block, *list(unpack(head_args)), nil)

    peers = []
    peers_consumed = 0

    declared = tuple(getattr(layout_obj, 'peers', ()) or ())
    if declared:
        declared_set = {str(s) for s in declared}
        for sibling in rest:
            if not _is_block(sibling):
                break
            sibling_tail = cdr(sibling)
            if is_nil(sibling_tail) or not is_pair(sibling_tail):
                break
            sibling_head_args = car(sibling_tail)
            if is_nil(sibling_head_args) or not is_pair(sibling_head_args):
                break
            sibling_head = car(sibling_head_args)
            if str(sibling_head) not in declared_set:
                break
            sibling_body_tail = cdr(sibling_tail)
            if is_nil(sibling_body_tail) or not is_pair(sibling_body_tail):
                break
            sibling_body_exprs = car(sibling_body_tail)
            peer_body_items = _expand_body(compiler, sibling_body_exprs)
            if peer_body_items:
                peer_body = cons(*peer_body_items, nil)
            else:
                peer_body = nil
            peers.append(cons(sibling_head_args, peer_body))
            peers_consumed += 1

    source = cons(head_args, expanded_body)
    result = layout_obj.read(source, peers)
    return result, peers_consumed


@special(symbol("compile/apply-layout"))
def special_apply_layout(compiler, source, tc=False):
    """
    Compile (compile/apply-layout BLOCK ...) — depth-first expand :block:
    cons literals through their layout handlers.

    Each BLOCK must be a :block: cons. Peer blocks are consumed from
    subsequent arguments when the primary layout declares peers. The
    expanded results are compiled directly; multiple results are wrapped
    in compile/begin before compilation.
    """

    called_by, args_tail = source

    args = list(unpack(args_tail))
    if not args:
        raise SpecialError(
            "compile/apply-layout requires at least one argument",
            source)

    for arg in args:
        if not _is_block(arg):
            raise SpecialError(
                "compile/apply-layout arguments must be :block: cons values",
                arg)

    results = []
    i = 0
    while i < len(args):
        expanded, consumed = _expand_block(compiler, args[i], args[i + 1:])
        results.append(expanded)
        i += 1 + consumed

    if len(results) == 1:
        form = results[0]
    else:
        form = cons(_symbol_begin, *results, nil)

    frag = compiler.fragment(called_by)
    frag.position_of(source)
    frag.merge(compiler.full_compile(form, tc=tc))
    return frag


# The end.
