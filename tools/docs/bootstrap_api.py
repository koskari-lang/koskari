#!/usr/bin/env python3

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
tools.docs.bootstrap_api

Generate Sphinx API autodoc stub pages under docs/api/

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
API = ROOT / 'docs' / 'api'


MODULES = [
    ('koskari', 'Koskari package'),
    ('koskari.asyncio', 'Asyncio integration'),
    ('koskari.builtins', 'Builtins (virtual module)'),
    ('koskari.cli', 'Command-line interface'),
    ('koskari.compile', 'Compiler'),
    ('koskari.compile.specials', 'Compiler special forms'),
    ('koskari.dis', 'Bytecode disassembler'),
    ('koskari.exceptions', 'Exceptions'),
    ('koskari.help', 'Help system'),
    ('koskari.hygiene', 'Macro hygiene'),
    ('koskari.importer', 'Import hooks'),
    ('koskari.lexer', 'Lexer'),
    ('koskari.lexer.lsp', 'Language server'),
    ('koskari.lexer.prompt_toolkit', 'prompt_toolkit REPL integration'),
    ('koskari.lib', 'Native extension (types, VM)'),
    ('koskari.matcher', 'Pattern matcher'),
    ('koskari.module', 'Module lifecycle'),
    ('koskari.reader', 'Reader'),
    ('koskari.reader.layouts', 'Layout handlers'),
    ('koskari.serialize', 'Serialization (codex)'),
    ('koskari.syntax', 'Syntax transformers'),
    ('koskari.types', 'Type predicates and helpers'),
]


def module_filename(modname):
    return modname.replace('.', '-') + '.md'


def page(modname, title, extra=''):
    extra_block = f'\n\n{extra}\n' if extra else '\n'
    return f'''# {title}

{extra_block}```{{eval-rst}}
.. automodule:: {modname}
   :members:
   :undoc-members:
   :show-inheritance:
```
'''


def main():
    API.mkdir(parents=True, exist_ok=True)

    builtins_note = (
        '```{admonition} Virtual module\n'
        ':class: note\n\n'
        'Importing ``koskari.builtins`` assembles the standard library from '
        'compiled bootstrap sources. The bootstrap loader package is not '
        'documented here.\n'
        '```'
    )

    for modname, title in MODULES:
        extra = builtins_note if modname == 'koskari.builtins' else ''
        (API / module_filename(modname)).write_text(
            page(modname, title, extra).strip() + '\n',
            encoding='utf-8')

    lines = [
        '# Python API reference',
        '',
        'Autodoc reference for Koskari Python modules. The `koskari.bootstrap`',
        'package is intentionally omitted (internal bootstrap loader only).',
        '',
        '```{toctree}',
        ':maxdepth: 1',
        '',
    ]
    for modname, _title in MODULES:
        lines.append(module_filename(modname).replace('.md', ''))
    lines.append('```')
    lines.append('')

    (API / 'index.md').write_text('\n'.join(lines), encoding='utf-8')
    print(f'Wrote {len(MODULES)} API module pages')


if __name__ == '__main__':
    main()


# The end.
