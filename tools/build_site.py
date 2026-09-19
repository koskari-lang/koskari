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
tools.build_site

Render the koskari.org landing page from docs/site into the published
site tree. The Sphinx documentation is built separately into a docs/
subdirectory of the same tree; this script only produces the top-level
marketing page and its static assets.

:author: Christopher O'Brien <obriencj@preoccupied.net>
:license: GNU General Public License v3
"""


import argparse
import re
import shutil
import sys
from configparser import ConfigParser
from html import escape
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, select_autoescape
from markupsafe import Markup


# Public repository. The GitHub link 404s for anonymous visitors until the
# repository is made public.
REPO_URL = "https://github.com/koskari-lang/koskari"
REPO_SHORT = "github.com/koskari-lang/koskari"

LICENSE_LABEL = "GPLv3 licensed"

# The domain the site is served from; emitted as a CNAME so GitHub Pages
# serves the custom domain from the deployed artifact.
SITE_DOMAIN = "koskari.org"

# Lockup (apple + "Koskari") is generated from koskari/assets/koskari-icon.svg
# by write_lockup_svg; keep placement and viewBox in sync here.
ICON_WORDMARK_VIEWBOX = "-7 -10 500 114"
ICON_WORDMARK_MARKUP = """
  <text x="110" y="92"
        font-family="Bricolage Grotesque, sans-serif"
        font-size="116" font-weight="700" fill="#211C16"
        letter-spacing="-0.02em">Koskari</text>
"""


# Sample programs shown on the landing page. Each one is a real file
# under docs/site/samples that runs and prints the output its trailing
# comments claim. The template reaches them as samples.<stem>_<ext>.
SAMPLE_DIR = 'samples'

SAMPLE_NAMES = (
    'hero.ki',
    'shapes.py',
    'shapes.ki',
    'fetch.ki',
    'retry.ki',
    'triples.ki',
)


def project_root(root=None):
    """
    Return the repository root path

    :param root: optional override; defaults to parent of tools/
    :return: Path to repository root
    """

    if root is not None:
        return Path(root).resolve()
    return Path(__file__).resolve().parent.parent


def read_version(root=None):
    """
    Read the current package version from setup.cfg metadata

    This is the same source Sphinx reads, so the landing page and the
    documentation always report the same version.

    :param root: repository root path
    :return: version string
    """

    conf = ConfigParser()
    conf.read([str(project_root(root) / "setup.cfg")])
    return conf["metadata"]["version"]


def token_classes():
    """
    Ordered Pygments token categories paired with landing page classes

    The first category containing a token wins, so the specific Name
    subtypes are listed ahead of the categories that contain them.
    Pygments is imported lazily on first call.

    :return: tuple of (token category, CSS class name) pairs
    :raises ImportError: when Pygments is not installed
    """

    from pygments.token import (
        Comment, Keyword, Name, Number, Operator, Punctuation, String)

    return (
        (Comment, 'k-cm'),
        (Name.Decorator, 'k-dec'),
        (Name.Function, 'k-fn'),
        (Name.Class, 'k-fn'),
        (Name.Builtin, 'k-kw'),
        (Name.Constant, 'k-type'),
        (Keyword, 'k-kw'),
        (String, 'k-str'),
        (Number, 'k-num'),
        (Operator, 'k-pn'),
        (Punctuation, 'k-pn'),
    )


def sample_lexer(name):
    """
    Return the Pygments lexer for one sample file

    Koskari samples use the lexer shipped with the package, so the
    landing page and the documentation highlight the same way.

    :param name: sample file name, used for its suffix
    :return: Pygments Lexer instance
    :raises ImportError: when Pygments is not installed
    """

    if name.endswith('.ki'):
        from koskari.extras.pygments import KoskariLexer
        return KoskariLexer()

    from pygments.lexers import PythonLexer
    return PythonLexer()


def highlight_sample(source, name):
    """
    Highlight one sample source into span markup for a code window

    :param source: sample file contents
    :param name: sample file name, used to pick the lexer
    :return: Markup holding the highlighted source
    """

    classes = token_classes()
    parts = []
    for tok, text in sample_lexer(name).get_tokens(source):
        if not text:
            continue
        body = escape(text, quote=False)
        for category, css in classes:
            if tok in category:
                parts.append(f'<span class="{css}">{body}</span>')
                break
        else:
            parts.append(body)
    return Markup("".join(parts).rstrip("\n"))


def samples(src):
    """
    Read and highlight every landing page sample

    :param src: source directory holding index.html.j2
    :return: dict keyed by sample stem and suffix, each value carrying
      the file name and its highlighted markup
    """

    found = {}
    for name in SAMPLE_NAMES:
        path = Path(src) / SAMPLE_DIR / name
        source = path.read_text(encoding='utf-8')
        key = name.replace('.', '_').replace('-', '_')
        found[key] = {
            "filename": name,
            "code": highlight_sample(source, name),
        }
    return found


def write_lockup_svg(out_path, root=None):
    """
    Build koskari-lockup.svg from the apple-only icon plus a wordmark

    The icon SVG stays mark-only. The lockup widens the viewBox and
    appends the Bricolage Grotesque label for img references on the
    landing page and docs chrome.

    :param out_path: destination path for koskari-lockup.svg
    :param root: repository root path
    :return: None
    """

    icon = project_root(root) / "koskari" / "assets" / "koskari-icon.svg"
    svg = icon.read_text(encoding="utf-8")
    svg = re.sub(
        r'\bviewBox="[^"]*"',
        f'viewBox="{ICON_WORDMARK_VIEWBOX}"',
        svg,
        count=1)
    svg = re.sub(r'\s*\bwidth="[^"]*"', '', svg, count=1)
    svg = re.sub(r'\s*\bheight="[^"]*"', '', svg, count=1)
    if "</svg>" not in svg:
        raise ValueError(f"icon SVG missing closing tag: {icon}")
    svg = svg.replace(
        "</svg>",
        ICON_WORDMARK_MARKUP.rstrip() + "\n</svg>",
        1)
    Path(out_path).write_text(svg, encoding="utf-8")


def render(src, out, root=None):
    """
    Render the landing page and copy its assets into the output tree

    :param src: source directory holding index.html.j2
    :param out: output directory to populate (created if absent)
    :param root: repository root, for reading the version from setup.cfg
    :return: None
    """

    src = Path(src)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)

    env = Environment(
        loader=FileSystemLoader(str(src)),
        autoescape=select_autoescape(["html", "xml"]),
        keep_trailing_newline=True,
    )
    template = env.get_template("index.html.j2")

    version = read_version(root)
    page = template.render(
        version=version,
        github_url=REPO_URL,
        repo_short=REPO_SHORT,
        license_label=LICENSE_LABEL,
        samples=samples(src),
    )
    (out / "index.html").write_text(page, encoding="utf-8")

    static = project_root(root) / "docs" / "_static"
    assets = project_root(root) / "koskari" / "assets"
    shutil.copyfile(static / "style.css", out / "style.css")
    shutil.copyfile(assets / "koskari-icon.svg", out / "koskari-icon.svg")
    lockup = static / "koskari-lockup.svg"
    if lockup.is_file():
        shutil.copyfile(lockup, out / "koskari-lockup.svg")
    else:
        write_lockup_svg(out / "koskari-lockup.svg", root=root)

    (out / "CNAME").write_text(f"{SITE_DOMAIN}\n", encoding="utf-8")


def main(args=None):
    """
    Command-line entry point

    :param args: optional argument list; defaults to sys.argv
    :return: process exit code
    """

    parser = argparse.ArgumentParser(
        description="Render the koskari.org landing page")
    parser.add_argument(
        "--out", default="build/site",
        help="output directory for the site root (default: build/site)")
    parser.add_argument(
        "--src", default=None,
        help="source directory (default: docs/site under the repo root)")
    options = parser.parse_args(args)

    root = project_root()
    src = options.src if options.src is not None else root / "docs" / "site"

    render(src, options.out, root=root)
    return 0


if __name__ == "__main__":
    sys.exit(main())


# The end.
