"""
Sphinx configuration for Koskari user documentation.
"""


import os
import sys
import koskari  # noqa: F401


def load_setup():
    from configparser import ConfigParser

    global project
    global release
    global version
    global author
    global copyright

    conf = ConfigParser()
    conf.read([os.path.join('..', 'setup.cfg')])
    metadata = conf['metadata']

    project = metadata['name']
    release = metadata['version']
    version = '.'.join(release.split('.')[:2])
    author = metadata['author']
    copyright = f"{metadata['copyright_years']}, {author}"


load_setup()


sys.path.insert(0, os.path.abspath('..'))


def skip_bootstrap(app, what, name, obj, skip, options):
    if name and name.startswith('koskari.bootstrap'):
        return True
    return skip


# Module docstrings carry a GPL header with `:author:`, `:license:`,
# and sometimes `:ai-assistant:` fields. These are per-file bookkeeping,
# not documentation, and the assistant field varies too much between
# tools to be worth surfacing on every rendered module page.
_HEADER_FIELDS = (':author:', ':license:', ':ai-assistant:')


def strip_header_fields(app, what, name, obj, options, lines):
    if what != 'module':
        return
    lines[:] = [
        line for line in lines
        if not line.strip().startswith(_HEADER_FIELDS)
    ]


def setup(app):
    app.connect('autodoc-skip-member', skip_bootstrap)
    app.connect('autodoc-process-docstring', strip_header_fields)


extensions = [
    'koskari.extras.sphinx',
    'myst_parser',
    'sphinx.ext.autodoc',
    'sphinx.ext.autosummary',
    'sphinx.ext.intersphinx',
    'sphinx.ext.napoleon',
    'sphinxcontrib.mermaid',
]

templates_path = ['_templates']
exclude_patterns = ['build', 'site', 'Thumbs.db', '.DS_Store']

root_doc = 'index'

html_theme = 'sphinx_rtd_theme'
html_theme_options = {
    'navigation_depth': 2,
    'collapse_navigation': True,
    'titles_only': True,
    'includehidden': True,
}
html_static_path = ['_static']
html_favicon = '_static/koskari-icon.svg'
html_logo = '_static/koskari-icon.svg'

# The docs are published under the /docs/ subpath of koskari.org; the
# landing page occupies the site root. This drives canonical URLs.
html_baseurl = 'https://koskari.org/docs/'

source_suffix = {
    '.rst': 'restructuredtext',
    '.md': 'markdown',
}

intersphinx_mapping = {
    'python': ('https://docs.python.org/3', None),
}

myst_enable_extensions = [
    'colon_fence',
    'deflist',
    'linkify',
]

myst_fence_as_directive = [
    'mermaid',
]

# Generate anchor slugs for headings h1-h4 so intra- and cross-page
# `[text](path#slug)` links resolve (used heavily by the VM reference).
myst_heading_anchors = 4

mermaid_version = '10.9.1'

autosummary_generate = True

autodoc_default_options = {
    'members': True,
    'undoc-members': True,
    'show-inheritance': True,
}

suppress_warnings = [
    'autodoc.duplicate_object',
]

# The end.
