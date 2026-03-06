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
koskari.lexer.lsp.server

LSP server entry point and registration.
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit LSP enhancements.


from lsprotocol.types import (
    TEXT_DOCUMENT_COMPLETION,
    TEXT_DOCUMENT_DID_CHANGE,
    TEXT_DOCUMENT_DID_OPEN,
    TEXT_DOCUMENT_HOVER,
)

from pygls.lsp.server import LanguageServer

from koskari.lexer.lsp.handlers import (
    handle_completion,
    handle_did_change,
    handle_did_open,
    handle_hover,
)


class KoskariLanguageServer(LanguageServer):
    """
    Language server for Koskari source.
    """

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.diagnostics = {}


def create_server():
    """
    Create and configure the Koskari LSP server.

    :return: KoskariLanguageServer with handlers registered
    """

    server = KoskariLanguageServer('koskari', '0.1')

    server.feature(TEXT_DOCUMENT_DID_OPEN)(handle_did_open)
    server.feature(TEXT_DOCUMENT_DID_CHANGE)(handle_did_change)
    server.feature(TEXT_DOCUMENT_HOVER)(handle_hover)
    server.feature(TEXT_DOCUMENT_COMPLETION)(handle_completion)

    return server


def main():
    """Entry point for koskari-lsp console script."""

    from pygls.cli import start_server
    start_server(create_server())


__all__ = (
    'KoskariLanguageServer',
    'create_server',
    'main',
)


# The end.
