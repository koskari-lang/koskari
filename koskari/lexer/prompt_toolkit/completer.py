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
koskari.lexer.prompt_toolkit.completer

Tab completion for Koskari REPL.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import re

from prompt_toolkit.completion import Completer, Completion

from ..tokens import _ATOM_TERMINATORS, LAYOUT_BLOCK_NAMES, SPECIAL_NAMES


# prompt_toolkit applies the pattern to the reversed text before the
# cursor, so the anchor holds the match to the atom ending at the cursor
# and the lookahead stops it at a leading ,@ prefix
ATOM_CHAR = f"[^{re.escape(_ATOM_TERMINATORS)}]"
ATOM_BEFORE_CURSOR_RE = re.compile(f"^(?:{ATOM_CHAR}+?(?=@,)|{ATOM_CHAR}+)")


def _completion_candidates_sorted(candidates):
    """
    Reverse lexicographic order, so a name sorts ahead of any name that
    is a proper prefix of it (e.g. compile/foo-bar before compile/foo)
    """

    return sorted(candidates, reverse=True)


def _namespace_keys(ns):
    """
    Extract completion candidate keys from a namespace.

    :param ns: dict, ModuleNamespace, or None
    :return: iterable of str
    """

    if ns is None:
        return ()
    if hasattr(ns, 'keys'):
        return ns.keys()
    if hasattr(ns, '__iter__'):
        return ns
    return ()


class KoskariCompleter(Completer):
    """
    Completer for Koskari special forms, layout heads, and namespace symbols.
    """

    def __init__(self, get_namespace=None, special_names=None):
        """
        Initialize the completer.

        :param get_namespace: callable() -> namespace or None; invoked on each
            completion request to get current REPL namespace for symbol completion
        :param special_names: optional frozenset of special-form names; defaults
            to ``SPECIAL_NAMES``
        """

        self.get_namespace = get_namespace
        sn = special_names if special_names is not None else SPECIAL_NAMES
        self._static = frozenset(sn | LAYOUT_BLOCK_NAMES)

    def get_completions(self, document, complete_event):
        """
        Yield completions for the atom before the cursor.

        :param document: prompt_toolkit Document
        :param complete_event: CompletionEvent
        :yield: Completion instances
        """

        word = document.get_word_before_cursor(pattern=ATOM_BEFORE_CURSOR_RE)
        if not word:
            return

        prefix_len = len(word)
        candidates = set(self._static)

        if self.get_namespace is not None:
            try:
                ns = self.get_namespace()
                for k in _namespace_keys(ns):
                    if isinstance(k, str) and not k.startswith('_'):
                        candidates.add(k)
            except Exception:
                pass

        for cand in _completion_candidates_sorted(candidates):
            if cand.startswith(word):
                yield Completion(cand, start_position=-prefix_len)


__all__ = ('KoskariCompleter',)


# The end.
