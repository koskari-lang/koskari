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
koskari.matcher

The pattern matcher behind the special forms, the macros and the match
special: the patterns and the special-form composites a shape is
declared with, the parsers of the explicit and case-pattern dialects,
and the functions that build and try matchers

:author: Christopher O'Brien <obriencj@gmail.com>
:license: GNU General Public License v3
"""


from .build import first_match
from .case import parse_case_pattern
from .forms import (
    BINDING, BINDINGS, BINDING_MATCHERS, BODY, CALLEDBY, DECL, DEFINE, FORMALS,
    USING, USING_FORMS, USING_MATCHERS, USING_UNPACK, VALUE, frame_formal_name)
from .heads import parse_explicit_rule_pattern
from .patterns import (
    APP, BIND, CAPTURE, CLASS, FIELD, FORM, GROUP, KEYWORD, LIST, LITERAL,
    LITERAL_KW, LITERAL_SYM, MAP, MAP_ENTRY, MATCH, NOT, ONEOF, OPTIONAL, PAIR,
    PREDICATE, REPEAT, REST, SAME, SYMBOL, TEST_ANY, TYPE, MatchResult,
    Pattern, check_pattern_arg, make_matcher, pattern_slot_names,
    repeat_capture)


__all__ = (
    'APP',
    'BIND',
    'BINDING',
    'BINDINGS',
    'BINDING_MATCHERS',
    'BODY',
    'CALLEDBY',
    'CAPTURE',
    'CLASS',
    'DECL',
    'DEFINE',
    'FIELD',
    'FORM',
    'FORMALS',
    'GROUP',
    'KEYWORD',
    'LIST',
    'LITERAL',
    'LITERAL_KW',
    'LITERAL_SYM',
    'MAP',
    'MAP_ENTRY',
    'MATCH',
    'MatchResult',
    'NOT',
    'ONEOF',
    'OPTIONAL',
    'PAIR',
    'PREDICATE',
    'Pattern',
    'REPEAT',
    'REST',
    'SAME',
    'SYMBOL',
    'TEST_ANY',
    'TYPE',
    'USING',
    'USING_FORMS',
    'USING_MATCHERS',
    'USING_UNPACK',
    'VALUE',
    'check_pattern_arg',
    'first_match',
    'frame_formal_name',
    'make_matcher',
    'parse_case_pattern',
    'parse_explicit_rule_pattern',
    'pattern_slot_names',
    'repeat_capture',
)


# The end.
