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
tools.editor-tokens.regen_editors_assets

Regenerate one editor asset: ``spec.yaml``, ``template.j2``, ``output`` (three
positional arguments only). Registers Jinja filters for regex alternation, JSON
string escaping, and Emacs ``concat`` line wrapping.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: Cursor Agent via Cursor.
"""


import argparse
import json
import sys
from pathlib import Path

import yaml
from jinja2 import Environment, FileSystemLoader, StrictUndefined


def _expand_block_header_tokens(layout_heads, layout_peers):
    """
    Expand layout heads and peer heads into a single ordered token list.

    For each layout head, emit the head followed by its declared peers (if any).
    """

    out = []
    for head in layout_heads:
        out.append(head)
        peers = layout_peers.get(head)
        if peers:
            out.extend(peers)
    return out

_REGEX_META = set(r"\.^$*+?{}[]|()\\")


def _escape_regex_token(token):
    """
    Escape a Koskari symbol token for inclusion inside a regex alternation.
    """

    parts = []
    for ch in token:
        if ch in _REGEX_META:
            parts.append("\\" + ch)
        else:
            parts.append(ch)
    return "".join(parts)


def _join_regex_alternation(tokens):
    """
    Join escaped tokens with ``|`` for a regex alternation.
    """

    return "|".join(_escape_regex_token(t) for t in tokens)


def _json_string_escape(fragment):
    """
    Escape a string for use inside a JSON double-quoted value (no quotes).
    """

    return json.dumps(fragment)[1:-1]


def _emacs_regex_alternation(tokens):
    """
    Build an Emacs regexp alternation string using ``\\|`` separators.
    """

    return "\\|".join(_escape_regex_token(t) for t in tokens)


def _emacs_regex_lines(tokens, indent):
    """
    Format Emacs ``concat`` fragment lines with trailing ``\\|`` continuations.
    """

    alt = _emacs_regex_alternation(tokens)
    return _emacs_regex_body_concat_lines(alt, indent, continuation=True)


def _emacs_elisp_quote_string(body):
    """
    Escape a regexp body for inclusion inside Emacs Lisp double-quoted strings.

    Each regexp backslash becomes two backslashes in the ``.el`` file; ``"``
    is backslash-escaped.
    """

    parts = []
    for ch in body:
        if ch == "\\":
            parts.append("\\\\")
        elif ch == '"':
            parts.append('\\"')
        else:
            parts.append(ch)
    return "".join(parts)


def _emacs_regex_body_concat_lines(body, indent, continuation):
    """
    Split a regexp body across lines like the committed ``concat`` fragments.

    When ``continuation`` is true, each non-final line ends with a ``\\|``
    separator (alternation continues on the next string).
    """

    lines = []
    start = 0
    max_chunk = 72
    body_len = len(body)
    while start < body_len:
        end = min(body_len, start + max_chunk)
        split_at = None
        if end < body_len:
            cut = body.rfind("\\|", start, end)
            if cut != -1 and cut > start:
                # Split before this ``\|`` so the chunk does not include it;
                # continuation lines append ``\|`` below (avoids ``\|\|``).
                end = cut
                split_at = cut
        chunk = body[start:end]
        if continuation and end < body_len:
            chunk = chunk + "\\|"
        lines.append(indent + '"' + _emacs_elisp_quote_string(chunk) + '"')
        if split_at is not None:
            start = split_at + len("\\|")
        else:
            start = end
    return "\n".join(lines) + "\n"


def _emacs_block_control_concat_lines(tokens, indent):
    """
    Build ``concat`` lines for the block-control keyword regexp group.

    ``indent`` is the leading whitespace before each string literal (set in the
    template so it lines up with surrounding ``concat`` fragments).
    """

    body = "\\(" + _emacs_regex_alternation(tokens) + "\\)"
    return _emacs_regex_body_concat_lines(body, indent, continuation=True)


def _emacs_list_head_concat_lines(tokens, indent):
    """
    Build ``concat`` lines for the list-head special-form alternation.
    """

    return _emacs_regex_lines(tokens, indent)


def _load_spec_context(spec_path):
    """
    Load YAML into the template context (lists/dicts only).
    """

    return yaml.safe_load(spec_path.read_text(encoding="ascii"))


def _jinja_env(templates_dir):
    """
    Jinja environment with Koskari editor filters and globals.
    """

    env = Environment(
        loader=FileSystemLoader(str(templates_dir)),
        autoescape=False,
        undefined=StrictUndefined,
        trim_blocks=False,
        lstrip_blocks=False)

    env.filters["regex_alternation"] = _join_regex_alternation
    env.filters["json_string_escape"] = _json_string_escape
    env.filters["emacs_concat_block"] = _emacs_block_control_concat_lines
    env.filters["emacs_concat_list_head"] = _emacs_list_head_concat_lines
    env.globals["block_header_tokens"] = _expand_block_header_tokens

    return env


def _render_one(spec_path, template_path, output_path):
    """
    Load YAML, render one template, write ``output_path``.
    """

    spec_path = Path(spec_path).resolve()
    template_path = Path(template_path).resolve()
    output_path = Path(output_path).resolve()

    templates_dir = template_path.parent
    template_name = template_path.name

    spec = _load_spec_context(spec_path)
    env = _jinja_env(templates_dir)
    template = env.get_template(template_name)
    rendered = template.render(**spec)
    if not rendered.endswith("\n"):
        rendered += "\n"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(rendered, encoding="ascii")


def main(argv=None):
    """
    Three positional arguments only: YAML spec, Jinja template, output file.
    """

    parser = argparse.ArgumentParser(
        description="Render one editor asset from YAML and a Jinja2 template")
    parser.add_argument(
        "spec_yaml",
        type=Path,
        help="path to the token spec YAML")
    parser.add_argument(
        "template",
        type=Path,
        help="path to the .j2 template")
    parser.add_argument(
        "output",
        type=Path,
        help="path to write the generated file")

    args = parser.parse_args(argv)
    _render_one(args.spec_yaml, args.template, args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())


# The end.
