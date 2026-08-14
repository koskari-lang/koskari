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
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, select_autoescape


# The next public beta target. Distinct from the current package version
# (which comes from setup.cfg); this is a roadmap milestone, not a release.
BETA_TARGET = "0.9.0"

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


# "Find your level" rows, aligned to the documentation we actually
# publish under /docs. Rendered in order; the number is derived from
# position, so reordering or inserting rows just works.
DOC_ROWS = [
    ("Getting Started", "/docs/start/overview/",
     "Install, open the REPL, evaluate your first form."),
    ("Learn Koskari", "/docs/start/beginner/",
     "The language from zero, one chapter at a time."),
    ("Coming from Python", "/docs/start/from-python/",
     "For people who already think in Python."),
    ("The Guide", "/docs/guide/",
     "A working tour: forms, functions, control flow, macros, and async."),
    ("Language Reference", "/docs/reference/",
     "Grammar, layout rules, special forms, and error codes."),
    ("Runtime & Builtins", "/docs/reference/runtime/",
     "Every builtin in the runtime library, indexed."),
]


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


def doc_rows():
    """
    Build the templating context for the documentation link rows

    :return: list of dicts with num, title, href, and blurb
    """

    rows = []
    for index, (title, href, blurb) in enumerate(DOC_ROWS, start=1):
        rows.append({
            "num": f"{index:02d}",
            "title": title,
            "href": href,
            "blurb": blurb,
        })
    return rows


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
        beta_target=BETA_TARGET,
        github_url=REPO_URL,
        repo_short=REPO_SHORT,
        license_label=LICENSE_LABEL,
        doc_rows=doc_rows(),
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
