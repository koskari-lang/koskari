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
koskari.help

Koskari help formatting and builtins

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Composer via Cursor
"""


from .tools import (
    HelpFormatter,
    help_into,
    help_register,
    help_registry_clear,
    help_registry_snapshot,
    help_unregister,
    kos_help,
    kos_help_text,
)

from .syntax import help_for_syntax  # registers Syntax handler
from .layout import help_for_layout_handler  # registers LayoutHandler


__all__ = (
    'HelpFormatter',
    'help_into',
    'help_register',
    'help_registry_clear',
    'help_registry_snapshot',
    'help_unregister',

    'kos_help',
    'kos_help_text',

    'help_for_syntax',
    'help_for_layout_handler',
)


# The end.
