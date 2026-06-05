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
koskari.extras.lsp

LSP server for Koskari source. Requires koskari[lsp] (pygls).
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit LSP enhancements.


from koskari.extras.lsp.server import create_server, main


__all__ = (
    'create_server',
    'main',
)


# The end.
