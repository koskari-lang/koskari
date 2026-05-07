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
matcher_ng

Python surface for the next-generation native matcher (matcher_ng).
Re-exports the native Matcher and MatchResult types together with the
ABSENT sentinel and a thaw helper that reconstructs a native Matcher
from its frozen tagged-tuple representation.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Claude Opus 4.7 via Cursor
"""


from koskari.lib import ABSENT, Matcher, MatchResult


__all__ = ["ABSENT", "Matcher", "MatchResult", "thaw"]


def thaw(tagged_tuple):
    """
    Reconstruct a native Matcher from its frozen tagged-tuple
    representation
    """

    return Matcher.from_tuple(tagged_tuple)


# The end.
