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
tools.corpus.views._material

Text helpers for the reference-material views (docs, programs).

Title and summary extraction are best-effort heuristics used only to
populate the ``title`` and ``summary`` columns. Link rewriting
(:func:`rewrite_markdown_links`) normalizes relative Markdown links in
the ``content`` column so an agent reading a stored page can resolve a
link straight through the MCP ``get_reference`` tool.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import posixpath
import re


_LINK_RE = re.compile(r'\[([^\]]+)\]\([^)]*\)')
_EMPHASIS_RE = re.compile(r'[*_`]+')
_LICENSE_MARKERS = (
    'redistribute',
    'without any warranty',
    'free software foundation',
    'general public license',
    'gnu ',
    'see <http',
    'copyright',
)


def _clean_inline(text):
    """
    Strip common inline Markdown so a summary reads as plain prose

    :param text: raw line or paragraph text
    :return: cleaned single-line text
    """

    text = _LINK_RE.sub(r'\1', text)
    text = _EMPHASIS_RE.sub('', text)
    return ' '.join(text.split())


def _strip_frontmatter(lines):
    """
    Drop a leading YAML frontmatter block delimited by ``---`` lines

    :param lines: list of source lines
    :return: lines with any leading frontmatter removed
    """

    if lines and lines[0].strip() == '---':
        for index in range(1, len(lines)):
            if lines[index].strip() == '---':
                return lines[index + 1:]
    return lines


def markdown_title_summary(text):
    """
    Extract a heading title and first-paragraph summary from Markdown

    :param text: raw Markdown document text
    :return: tuple of (title or None, summary or None)
    """

    lines = _strip_frontmatter(text.splitlines())

    title = None
    for line in lines:
        match = re.match(r'#+\s+(.*)', line)
        if match:
            title = _clean_inline(match.group(1))
            break

    summary = None
    in_fence = False
    paragraph = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('```'):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if stripped.startswith('#') or not stripped:
            if paragraph:
                break
            continue
        paragraph.append(stripped)
    if paragraph:
        summary = _clean_inline(' '.join(paragraph))

    return title, summary


_MD_LINK_RE = re.compile(r'(!?\[[^\]]*\]\()([^)]+)(\))')
_SCHEME_RE = re.compile(r'[A-Za-z][A-Za-z0-9+.-]*:')
_TITLE_RE = re.compile(r'(\S+)(\s+["\'].*)\Z')
_TOCTREE_TITLE_RE = re.compile(r'(.*<)([^>]+)(>)\Z')
_GLOB_CHARS = frozenset('*?[]')


def _normalize_link_target(dest, base_dir):
    """
    Resolve one Markdown link destination to a root-relative path

    Rewrites a repo-internal relative link (possibly containing ``../``)
    into a path rooted at the view's content tree, matching the scheme
    used by the row ``path`` column. External links, ``mailto:``, pure
    ``#anchor`` fragments, site-absolute paths, and links that escape the
    tree are left unchanged.

    :param dest: raw destination text from inside the link parentheses
    :param base_dir: directory of the linking file, as a root-relative
        posix path
    :return: the rewritten destination, or None to leave it unchanged
    """

    stripped = dest.strip()
    if not stripped:
        return None

    title = ''
    match = _TITLE_RE.match(stripped)
    if match:
        target, title = match.group(1), match.group(2)
    else:
        target = stripped

    if target.startswith('<') and target.endswith('>'):
        target = target[1:-1]

    if _SCHEME_RE.match(target) or target[:1] in ('#', '/'):
        return None

    fragment = ''
    hash_index = target.find('#')
    if hash_index >= 0:
        target, fragment = target[:hash_index], target[hash_index:]
    if not target:
        return None

    resolved = posixpath.normpath(posixpath.join(base_dir, target))
    if resolved.startswith('..'):
        return None

    return resolved + fragment + title


def _normalize_toctree_entry(entry, base_dir):
    """
    Resolve one ``{toctree}`` child entry to a root-relative page path

    Sphinx toctree entries name a page by its extension-less docname
    (``map``, ``concepts/index``) or as ``Title <docname>``. Rewrites the
    docname to the root-relative ``.md`` path matching the row ``path``
    column. Glob patterns, ``self``, external, and absolute entries are
    left unchanged.

    :param entry: the stripped toctree line text
    :param base_dir: directory of the index file, as a root-relative
        posix path
    :return: the rewritten entry, or None to leave it unchanged
    """

    match = _TOCTREE_TITLE_RE.match(entry)
    if match:
        prefix, target, suffix = match.group(1), match.group(2).strip(), \
            match.group(3)
    else:
        prefix, target, suffix = '', entry, ''

    if not target or _GLOB_CHARS.intersection(target):
        return None
    if _SCHEME_RE.match(target) or target[:1] in ('#', '/') or target == 'self':
        return None

    resolved = posixpath.normpath(posixpath.join(base_dir, target))
    if resolved.startswith('..'):
        return None
    if not resolved.endswith('.md'):
        resolved += '.md'
    return prefix + resolved + suffix


def _rewrite_toctree_line(line, base_dir):
    """
    Rewrite a single non-glob ``{toctree}`` body line, preserving layout

    :param line: raw line including leading indent and line ending
    :param base_dir: directory of the index file, root-relative posix
    :return: the line with its entry rewritten, or unchanged
    """

    body = line.rstrip('\r\n')
    ending = line[len(body):]
    stripped = body.strip()
    if not stripped or stripped.startswith(':'):
        return line
    rewritten = _normalize_toctree_entry(stripped, base_dir)
    if rewritten is None:
        return line
    leading = body[:len(body) - len(body.lstrip())]
    return leading + rewritten + ending


def rewrite_markdown_links(text, rel_path):
    """
    Rewrite relative Markdown links to root-relative content paths

    Applies :func:`_normalize_link_target` to every inline ``[text](dest)``
    link outside fenced code blocks, and :func:`_normalize_toctree_entry`
    to the child entries of non-glob ``{toctree}`` directives, so an agent
    reading the stored ``content`` can resolve a link or a navigation entry
    straight through ``get_reference``.

    :param text: raw Markdown document text
    :param rel_path: the document's own root-relative posix path
    :return: text with intra-repo relative links and toctree entries
        rewritten
    """

    base_dir = posixpath.dirname(rel_path)

    def _sub(match):
        replaced = _normalize_link_target(match.group(2), base_dir)
        if replaced is None:
            return match.group(0)
        return match.group(1) + replaced + match.group(3)

    out = []
    fence = None
    toctree_glob = False
    for line in text.splitlines(keepends=True):
        marker = line.lstrip()
        if fence is None:
            if marker.startswith('```') or marker.startswith('~~~'):
                if marker.startswith('```') and 'toctree' in marker:
                    fence, toctree_glob = 'toctree', False
                else:
                    fence = 'code'
            else:
                line = _MD_LINK_RE.sub(_sub, line)
            out.append(line)
        elif marker.startswith('```') or marker.startswith('~~~'):
            fence = None
            out.append(line)
        elif fence != 'toctree':
            out.append(line)
        elif marker.strip() == ':glob:':
            toctree_glob = True
            out.append(line)
        elif toctree_glob:
            out.append(line)
        else:
            out.append(_rewrite_toctree_line(line, base_dir))
    return ''.join(out)


def _extract_module_docstring(lines, start):
    """
    Read a triple-double-quoted block beginning at or after ``start``

    :param lines: list of source lines
    :param start: index of the line opening the docstring
    :return: first non-empty line of the docstring, or None
    """

    body = []
    first = lines[start].lstrip()
    if not first.startswith('"""'):
        return None
    remainder = first[3:]
    if '"""' in remainder:
        return _clean_inline(remainder.split('"""', 1)[0]) or None

    body.append(remainder)
    for line in lines[start + 1:]:
        if '"""' in line:
            body.append(line.split('"""', 1)[0])
            break
        body.append(line)
    for line in body:
        cleaned = _clean_inline(line)
        if cleaned:
            return cleaned
    return None


def _is_boilerplate_comment(line, stem):
    """
    Return True for comment lines that are not a useful summary

    Filters the leading file-path banner, GPL license text, and
    ``:field:`` metadata lines common to Koskari source headers.

    :param line: comment text with leading ``;`` markers removed
    :param stem: file stem, used to spot the path banner line
    :return: True when the line should be skipped
    """

    if not line:
        return True
    lowered = line.lower()
    if line.startswith(':'):
        return True
    if line.endswith('.ki') or ('/' in line and stem in line):
        return True
    for marker in _LICENSE_MARKERS:
        if marker in lowered:
            return True
    return False


def ki_title_summary(text, stem):
    """
    Extract a title and summary from a Koskari source file

    Prefers a top-level triple-double-quoted module docstring; otherwise
    falls back to the first substantive line of the leading ``;`` comment
    banner.

    :param text: raw Koskari source text
    :param stem: file stem used as the title and to skip the path banner
    :return: tuple of (title, summary or None)
    """

    lines = text.splitlines()
    comments = []
    index = 0
    while index < len(lines):
        stripped = lines[index].strip()
        if not stripped:
            index += 1
            continue
        if stripped.startswith(';'):
            comments.append(stripped.lstrip(';').strip())
            index += 1
            continue
        break

    summary = None
    if index < len(lines) and lines[index].lstrip().startswith('"""'):
        summary = _extract_module_docstring(lines, index)

    if summary is None:
        for comment in comments:
            if not _is_boilerplate_comment(comment, stem):
                summary = _clean_inline(comment)
                break

    return stem, summary


# The end.
