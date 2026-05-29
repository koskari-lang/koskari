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
tools.docs.split_reference

Split design reference markdown into per-element pages under docs/reference/

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
:ai-assistant: claude-4.6-sonnet-high via Cursor
"""


import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DESIGN = ROOT / 'design'
DOCS = ROOT / 'docs'
DOCS_REF = DOCS / 'reference'


def design_prefix(doc_rel_path):
    """
    Relative URL prefix from a docs/ path to design/
    """

    parts = Path(doc_rel_path).parts
    ups = len(parts) - 1
    return '../' * ups + 'design/'


def repo_prefix(doc_rel_path):
    """
    Relative URL prefix from a docs/ path to repository root
    """

    parts = Path(doc_rel_path).parts
    ups = len(parts) - 1
    return '../' * ups


def fix_body_links(body, doc_rel_path):
    """
    Rewrite design-relative markdown links for Sphinx doc paths
    """

    dp = design_prefix(doc_rel_path)
    rp = repo_prefix(doc_rel_path)

    def sub_design(match):
        target = match.group(1)
        if target.startswith('design/'):
            return f']({rp}{target})'
        if target.startswith('../'):
            return match.group(0)
        if target.startswith('koskari/') or target.startswith('../../koskari/'):
            clean = target.lstrip('./')
            while clean.startswith('../'):
                clean = clean[3:]
            return f']({rp}{clean})'
        if '/' in target and not target.startswith('http'):
            return f']({dp}{target})'
        if target.endswith('.md'):
            return f']({dp}{target})'
        return match.group(0)

    body = re.sub(r'\]\(([^)]+)\)', sub_design, body)
    return body


def wrap_page(title, source_name, body, doc_rel_path):
    """
    Wrap extracted body text in a standard reference page header
    """

    body = fix_body_links(body.strip(), doc_rel_path)
    if not body:
        body = '_No content extracted._'

    dp = design_prefix(doc_rel_path)

    return (
        f'# {title}\n\n'
        '```{admonition} Reference status\n'
        ':class: note\n\n'
        f'Adapted from design/{source_name}. Stability: evolving unless '
        'noted otherwise.\n'
        '```\n\n'
        f'{body}\n\n'
        '---\n\n'
        f'Source: [design/{source_name}]({dp}{source_name})\n'
    )


def symbol_slug(symbol):
    """
    Map a Koskari symbol or opcode name to a filename slug
    """

    slug = symbol.strip()
    if slug.startswith('KOS_OP_'):
        return 'kos-op-' + slug[7:].lower().replace('_', '-')
    slug = slug.replace('compile/', 'compile-')
    slug = slug.replace('layout/', 'layout-')
    slug = slug.replace('/', '-')
    slug = slug.replace('?', '')
    slug = slug.replace('*', 'star')
    slug = slug.replace('+', 'plus')
    slug = slug.replace('=', 'eq')
    slug = slug.replace('<', 'lt')
    slug = slug.replace('>', 'gt')
    slug = slug.replace('|', 'or')
    slug = slug.replace('&', 'and')
    slug = slug.replace('@', 'at')
    slug = slug.replace('~', 'tilde')
    slug = slug.replace('%', 'mod')
    slug = slug.replace('!', '')
    slug = re.sub(r'[^a-zA-Z0-9_-]+', '-', slug)
    slug = re.sub(r'-+', '-', slug)
    return slug.strip('-').lower()


def split_by_headings(text, level):
    """
    Split markdown text into (heading, body) pairs at the given heading level
    """

    pattern = re.compile(rf'^(?={"#" * level} )(.+)$', re.MULTILINE)
    matches = list(pattern.finditer(text))
    if not matches:
        return []

    sections = []
    for idx, match in enumerate(matches):
        start = match.start()
        end = matches[idx + 1].start() if idx + 1 < len(matches) else len(text)
        heading = match.group(1).strip()
        body = text[match.end():end].strip()
        sections.append((heading, body))
    return sections


def write_page(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding='utf-8')


def write_index(path, title, intro, glob_pattern, maxdepth=1):
    intro = intro.strip()
    content = (
        f'# {title}\n\n'
        f'{intro}\n\n'
        '```{toctree}\n'
        f':maxdepth: {maxdepth}\n'
        ':glob:\n\n'
        f'{glob_pattern}\n'
        '```\n'
    )
    write_page(path, content)


def write_wrapped(path, title, source_name, body):
    rel = path.relative_to(DOCS)
    write_page(path, wrap_page(title, source_name, body, str(rel)))


def split_opcodes():
    text = (DESIGN / 'OPCODES.md').read_text(encoding='utf-8')
    sections = split_by_headings(text, 2)

    overview_parts = []
    families_body = None
    opcode_sections = []

    for heading, body in sections:
        if heading.startswith('## 1.'):
            overview_parts.append(f'## {heading[3:]}\n\n{body}')
        elif heading.startswith('## 2.'):
            families_body = body
        elif heading.startswith('## See also') or heading.startswith('## Document'):
            continue
        elif re.match(r'^## \d', heading):
            overview_parts.append(f'## {heading[3:]}\n\n{body}')

    overview = '\n\n'.join(overview_parts)
    write_wrapped(
        DOCS_REF / 'vm' / 'index.md',
        'Koskari bytecode overview', 'OPCODES.md', overview)

    if families_body:
        write_wrapped(
            DOCS_REF / 'vm' / 'families.md',
            'Opcode families', 'OPCODES.md', families_body)

    level3 = split_by_headings(text, 3)
    for heading, body in level3:
        if not heading.startswith('### KOS_OP_'):
            continue
        name = heading.replace('### ', '').split()[0]
        name = re.sub(r'\s*\{#.*\}$', '', name)
        slug = symbol_slug(name)
        write_wrapped(
            DOCS_REF / 'vm' / 'opcodes' / f'{slug}.md',
            name, 'OPCODES.md', body)

    count = len(list((DOCS_REF / 'vm' / 'opcodes').glob('*.md')))
    write_index(
        DOCS_REF / 'vm' / 'opcodes' / 'index.md',
        'Opcode reference',
        'Per-opcode reference pages. See also '
        '[Bytecode overview](../index.md) and [Opcode families](../families.md).',
        '*\n   kos-op-*',
        maxdepth=1)
    return count


SPECIALS_SKIP = frozenset([
    '## Macros, reader macros, hygiene, and compiler optimizations',
    '## Object operator specials',
    '## Other implemented specials',
    '## Adding New Specials',
])


def extract_object_operator_symbols(text):
    """
    Find compile/ symbols mentioned in the object operator specials section
    """

    start = text.find('## Object operator specials')
    end = text.find('## compile/call/cc', start)
    if start < 0 or end < 0:
        return [], ''

    section = text[start:end]
    symbols = sorted(set(re.findall(r'`(compile/[^`]+)`', section)))
    return symbols, section


def split_specials():
    text = (DESIGN / 'SPECIALS.md').read_text(encoding='utf-8')
    preamble_end = text.find('\n## compile/bytecode')
    preamble = text[:preamble_end].strip() if preamble_end > 0 else ''

    count = 0
    for heading, body in split_by_headings(text, 2):
        if heading in SPECIALS_SKIP:
            continue
        if heading.startswith('## compile/') or heading in (
                '## import', '## import-from', '## with', '## dyn-with'):
            title = heading[3:]
            slug = symbol_slug(title)
            write_wrapped(
                DOCS_REF / 'specials' / f'{slug}.md',
                title, 'SPECIALS.md', body)
            count += 1

    symbols, object_section = extract_object_operator_symbols(text)
    shared_intro = ''
    if object_section:
        intro_end = object_section.find('### Constant folding')
        if intro_end > 0:
            shared_intro = object_section[len('## Object operator specials'):intro_end]
        folding_end = object_section.find('### Comparison interface')
        if folding_end > 0:
            shared_intro += '\n\n' + object_section[
                object_section.find('### Constant folding'):folding_end]

    write_wrapped(
        DOCS_REF / 'specials' / 'object-operators' / 'index.md',
        'Object operator specials',
        'SPECIALS.md',
        (shared_intro.strip() or 'Shared notes for compile-time object '
         'operator specials.'))

    for sym in symbols:
        slug = symbol_slug(sym)
        sym_path = DOCS_REF / 'specials' / f'{slug}.md'
        dp = design_prefix(str(sym_path.relative_to(DOCS)))
        sym_body = (
            f'**Symbol:** `{sym}`\n\n'
            'See [Object operator overview](object-operators/index.md) and '
            f'[design/SPECIALS.md]({dp}SPECIALS.md) '
            'for the full operator tables.\n'
        )
        write_wrapped(sym_path, sym, 'SPECIALS.md', sym_body)
        count += 1

    intro = preamble
    if intro:
        intro = intro.split('\n', 1)[-1].strip()
    write_index(
        DOCS_REF / 'specials' / 'index.md',
        'Special forms',
        intro or 'Compiler special forms implemented in Koskari.',
        '*\n   compile-*\n   import*\n   with*\n   dyn-with*',
        maxdepth=1)
    return count


def split_layouts():
    text = (DESIGN / 'LAYOUTS.md').read_text(encoding='utf-8')
    preamble_end = text.find('\n## layout/')
    preamble = text[:preamble_end].strip() if preamble_end > 0 else ''
    count = 0

    for heading, body in split_by_headings(text, 2):
        if not heading.startswith('## layout/'):
            continue
        title = heading[3:]
        if title == 'layout/while and layout/for-each':
            while_body = body
            for_each_note = (
                '\n\nSee also layout/while above; for-each uses the same '
                'handler family with different head routing.'
            )
            write_wrapped(
                DOCS_REF / 'layouts' / 'layout-while.md',
                'layout/while', 'LAYOUTS.md', while_body)
            write_wrapped(
                DOCS_REF / 'layouts' / 'layout-for-each.md',
                'layout/for-each', 'LAYOUTS.md',
                while_body + for_each_note)
            count += 2
            continue
        slug = symbol_slug(title)
        write_wrapped(
            DOCS_REF / 'layouts' / f'{slug}.md',
            title, 'LAYOUTS.md', body)
        count += 1

    intro = preamble.split('\n', 1)[-1].strip() if preamble else ''
    write_index(
        DOCS_REF / 'layouts' / 'index.md',
        'Layout handlers',
        intro or 'Indent-based block syntax resolved at read time.',
        'layout-*',
        maxdepth=1)
    return count


def split_macros():
    text = (DESIGN / 'MACROS.md').read_text(encoding='utf-8')
    preamble_end = text.find('\n## koskari/bootstrap/')
    preamble = text[:preamble_end].strip() if preamble_end > 0 else ''
    count = 0

    for heading, body in split_by_headings(text, 3):
        title = heading.replace('### ', '').strip()
        if title.endswith('(macro)'):
            title = title[:-7].strip()
        slug = symbol_slug(title)
        if not slug or slug.startswith('koskari-bootstrap'):
            continue
        write_wrapped(
            DOCS_REF / 'macros' / f'{slug}.md',
            title, 'MACROS.md', body)
        count += 1

    intro = preamble.split('\n', 1)[-1].strip() if preamble else ''
    write_index(
        DOCS_REF / 'macros' / 'index.md',
        'Macros',
        intro or 'Compile-time macros from the Koskari bootstrap library.',
        '*',
        maxdepth=1)
    return count


def main():
    opcodes = split_opcodes()
    specials = split_specials()
    layouts = split_layouts()
    macros = split_macros()
    print(f'Wrote {opcodes} opcode pages')
    print(f'Wrote {specials} special pages')
    print(f'Wrote {layouts} layout pages')
    print(f'Wrote {macros} macro pages')


if __name__ == '__main__':
    main()


# The end.
