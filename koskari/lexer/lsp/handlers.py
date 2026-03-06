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
koskari.lexer.lsp.handlers

LSP request/notification handlers for diagnostics, hover, and completion.
"""

# This file was entirely generated via prompt, as the basis for the
# prompt_toolkit LSP enhancements.


from lsprotocol.types import (
    CompletionItem,
    CompletionItemKind,
    CompletionList,
    CompletionParams,
    Diagnostic,
    DiagnosticSeverity,
    DidChangeTextDocumentParams,
    DidOpenTextDocumentParams,
    Hover,
    HoverParams,
    MarkupContent,
    MarkupKind,
    PublishDiagnosticsParams,
)

from koskari.exceptions import LayoutError, ReaderError
from koskari.lexer import LAYOUT_HEAD_NAMES, SPECIAL_NAMES, parse_all, tokenize
from koskari.lexer.lsp.utils import get_word_at_position, koskari_to_lsp_range


def collect_diagnostics(source, uri):
    """
    Parse source and collect diagnostics for any ReaderError.

    :param source: full document source string
    :param uri: document URI for reporting
    :return: list of Diagnostic
    """

    diagnostics = []

    try:
        for _ in parse_all(source, uri):
            pass
    except ReaderError as exc:
        location = getattr(exc, 'location', None)
        message = getattr(exc, 'message', str(exc))
        line_content = None
        if location and source:
            lines = source.splitlines()
            if 1 <= location[0] <= len(lines):
                line_content = lines[location[0] - 1]
        rng = koskari_to_lsp_range(location, line_content)
        diagnostics.append(Diagnostic(
            range=rng,
            message=message,
            severity=DiagnosticSeverity.Error))
    except Exception:
        diagnostics.append(Diagnostic(
            range=koskari_to_lsp_range((1, 0)),
            message='Unexpected parse error',
            severity=DiagnosticSeverity.Error))

    return diagnostics


def handle_did_open(ls, params):
    """Handle textDocument/didOpen: parse and publish diagnostics."""

    doc = ls.workspace.get_text_document(_get_uri(params.text_document))
    diagnostics = collect_diagnostics(doc.source, doc.uri)
    ls.diagnostics[doc.uri] = (doc.version, diagnostics)
    ls.text_document_publish_diagnostics(
        PublishDiagnosticsParams(
            uri=doc.uri,
            version=doc.version,
            diagnostics=diagnostics))


def handle_did_change(ls, params):
    """Handle textDocument/didChange: parse and publish diagnostics."""

    doc = ls.workspace.get_text_document(_get_uri(params.text_document))
    diagnostics = collect_diagnostics(doc.source, doc.uri)
    ls.diagnostics[doc.uri] = (doc.version, diagnostics)
    ls.text_document_publish_diagnostics(
        PublishDiagnosticsParams(
            uri=doc.uri,
            version=doc.version,
            diagnostics=diagnostics))


def _get_uri(text_document):
    if hasattr(text_document, 'uri'):
        return text_document.uri
    if isinstance(text_document, dict):
        return text_document.get('uri')
    return None


def handle_completion(ls, params):
    """Handle textDocument/completion: return special forms and layout heads."""

    doc = ls.workspace.get_text_document(_get_uri(params.text_document))
    prefix = get_word_at_position(doc, params.position)
    candidates = SPECIAL_NAMES | LAYOUT_HEAD_NAMES

    if prefix:
        matching = [c for c in candidates if c.startswith(prefix)]
    else:
        matching = sorted(candidates)

    return CompletionList(
        is_incomplete=False,
        items=[
            CompletionItem(label=c, kind=CompletionItemKind.Function)
            for c in matching])


def _token_at_position(source, line_1, col_1):
    """
    Find the token at 1-based (line, col) using tokenize.

    :return: (Token, None) or (None, None) if no token
    """

    for tok in tokenize(source, '<doc>'):
        if (tok.start_line, tok.start_col) <= (line_1, col_1) < (tok.end_line, tok.end_col):
            return (tok, None)
        if tok.start_line > line_1:
            break
    return (None, None)


def _hover_for_symbol(name):
    """Return hover content for a special or layout name."""

    if name in SPECIAL_NAMES:
        return 'Special form: `' + name + '`'
    if name in LAYOUT_HEAD_NAMES:
        return 'Layout: `' + name + '`'
    return None


def handle_hover(ls, params):
    """Handle textDocument/hover: return doc for special/layout at position."""

    doc = ls.workspace.get_text_document(_get_uri(params.text_document))
    pos = params.position
    line_1 = pos.line + 1
    col_1 = pos.character + 1

    tok, _ = _token_at_position(doc.source, line_1, col_1)
    if tok is None or tok.type != 'symbol' and tok.type != 'special' and tok.type != 'layout':
        return None

    content = _hover_for_symbol(tok.text)
    if content is None:
        return None

    from lsprotocol.types import Position, Range

    rng = Range(
        start=Position(line=tok.start_line - 1, character=tok.start_col - 1),
        end=Position(line=tok.end_line - 1, character=tok.end_col - 1))

    return Hover(
        contents=MarkupContent(kind=MarkupKind.Markdown, value=content),
        range=rng)


__all__ = (
    'collect_diagnostics',
    'handle_completion',
    'handle_did_change',
    'handle_did_open',
    'handle_hover',
)


# The end.
